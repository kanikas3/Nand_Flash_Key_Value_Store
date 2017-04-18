/**
 * This file contains the prototype core functionalities.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "core.h"
#include "device.h"

#define PRINT_PREF KERN_INFO "[LKP_KV]: "


static int init_config(int mtd_index, lkp_kv_cfg *config);
static void print_config(lkp_kv_cfg *config);
static int init_scan(lkp_kv_cfg *config);
static void print_config(lkp_kv_cfg *config);
int read_page(int page_index, char *buf, lkp_kv_cfg *config);
int write_page(int page_index, const char *buf, lkp_kv_cfg *config);
static void data_format_callback(struct erase_info *e);
static void metadata_format_callback(struct erase_info *e);
static int format_config(lkp_kv_cfg *config,
			 void (*callback)(struct erase_info *e));
static void destroy_config(lkp_kv_cfg *config);

lkp_kv_cfg meta_config;
lkp_kv_cfg data_config;


/**
 * Module initialization function
 */
static int __init lkp_kv_init(void)
{
	printk(PRINT_PREF "Loading... \n");

	if (init_config(0, &meta_config) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (init_config(1, &data_config) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -1;
	}

	return 0;
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	printk(PRINT_PREF "Exiting ... \n");
	device_exit();
	destroy_config(&meta_config);
	destroy_config(&data_config);
}

/**
 * Freeing stuff on exit
 */
static void destroy_config(lkp_kv_cfg *config)
{
	int i;
	put_mtd_device(config->mtd);
	for (i = 0; i < config->nb_blocks; i++)
		vfree(config->blocks[i].pages_states);
	vfree(config->blocks);
}

/**
 * Global state initialization, return 0 when ok, -1 on error
 */
static int init_config(int mtd_index, lkp_kv_cfg *config)
{
	uint64_t tmp_blk_num;

	if (mtd_index == -1) {
		printk(PRINT_PREF
		       "Error, flash partition index missing, should be"
		       " indicated for example like this: MTD_INDEX=5\n");
		return -1;
	}

	config->format_done = 0;
	config->read_only = 0;

	config->mtd_index = mtd_index;

	/* The flash partition is manipulated by caling the driver, through the
	 * mtd_info object. There is one of these object per flash partition */
	config->mtd = get_mtd_device(NULL, mtd_index);

	if (config->mtd == NULL)
		return -1;

	config->block_size = config->mtd->erasesize;
	config->page_size = config->mtd->writesize;
	config->pages_per_block = config->block_size / config->page_size;

	tmp_blk_num = config->mtd->size;
	do_div(tmp_blk_num, (uint64_t) config->mtd->erasesize);
	config->nb_blocks = (int)tmp_blk_num;

	/* Semaphore initialized to 1 (available) */
	sema_init(&config->format_lock, 1);

	print_config(config);

	/* Flash scan for metadata creation: which flash blocks and pages are
	 * free/occupied */
	if (init_scan(config) != 0) {
		printk(PRINT_PREF "Init scan error\n");
		return -1;
	}

	return 0;
}

/**
 * Print some statistics on the kernel log
 */
void print_config(lkp_kv_cfg *config)
{
	printk(PRINT_PREF "Config : \n");
	printk(PRINT_PREF "=========\n");

	printk(PRINT_PREF "mtd_index: %d\n", config->mtd_index);
	printk(PRINT_PREF "nb_blocks: %d\n", config->nb_blocks);
	printk(PRINT_PREF "block_size: %d\n", config->block_size);
	printk(PRINT_PREF "page_size: %d\n", config->page_size);
	printk(PRINT_PREF "pages_per_block: %d\n", config->pages_per_block);
	printk(PRINT_PREF "read_only: %d\n", config->read_only);
}

/**
 * Launch time metadata creation: flash is scanned to determine which flash
 * blocs and pages are free/occupied. Return 0 when ok, -1 on error
 */
int init_scan(lkp_kv_cfg *config)
{
	int i, j;
	char *buffer;

	buffer = (char *)vmalloc(config->page_size * sizeof(char));

	/* metadata initialization */
	config->blocks =
	    (blk_info *) vmalloc((config->nb_blocks) * sizeof(blk_info));
	for (i = 0; i < config->nb_blocks; i++) {
		config->blocks[i].state = BLK_FREE;
		config->blocks[i].pages_states =
		    (page_state *) vmalloc(config->pages_per_block *
					   sizeof(page_state));
		for (j = 0; j < config->pages_per_block; j++)
			config->blocks[i].pages_states[j] = PG_FREE;
	}

	/* scan: each flash page is read sequentially */
	for (i = 0; i < config->nb_blocks; i++) {
		for (j = 0; j < config->pages_per_block; j++) {
			int key_len;
			if (read_page(i * config->pages_per_block + j,
				      buffer, config) !=
			    0)
				goto err_free;
			memcpy(&key_len, buffer, sizeof(int));

			/* If there are only ones at the beginning of the flash page, the page
			 * is free */
			if (key_len == 0xFFFFFFFF)
				break;
			else {
				/* otherwise the page contains something */
				config->blocks[i].state = BLK_USED;
				config->blocks[i].pages_states[j] = PG_VALID;
				/* If all pages contain something: switch to read-only mode */
				if ((i == (config->nb_blocks - 1))
				    && (j == (config->pages_per_block - 1)))
					config->read_only = 1;
			}
		}
	}

	vfree(buffer);

	return 0;

err_free:
	vfree(buffer);
	for (i = 0; i < config->nb_blocks; i++)
		vfree(config->blocks[i].pages_states);
	vfree(config->blocks);
	return -1;
}


int read_bytes (int page_index, char *buf, lkp_kv_cfg *config,
			size_t bytes)
{
	uint64_t addr;
	size_t retlen;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config->page_size);

	/* call the NAND driver MTD to perform the read operation */
	return config->mtd->_read(config->mtd, addr, bytes, &retlen,
				 buf);
}

/**
 * Read the flash page with index page_index, data read are placed in buf
 * Retourne 0 when ok, something else on error
 */
int read_page(int page_index, char *buf, lkp_kv_cfg *config)
{
	uint64_t addr;
	size_t retlen;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config->page_size);

	/* call the NAND driver MTD to perform the read operation */
	return config->mtd->_read(config->mtd, addr, config->page_size, &retlen,
				 buf);
}

/**
 * Write the flash page with index page_index, data to write is in buf.
 * Returns:
 * 0 on success
 * -1 if we are in read-only mode
 * -2 when a write error occurs
 */
int write_page(int page_index, const char *buf, lkp_kv_cfg *config)
{
	uint64_t addr;
	size_t retlen;

	/* if the flash partition is full, dont write */
	if (config->read_only)
		return -1;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config->page_size);

	/* call the NAND driver MTD to perform the write operation */
	if (config->mtd->
	    _write(config->mtd, addr, config->page_size, &retlen, buf) != 0)
		return -2;

	/* update metadata to set the written page state, it is now valid */
	config->blocks[page_index /
		      config->pages_per_block].pages_states[page_index %
							   config->
							   pages_per_block] =
	    PG_VALID;

	return 0;
}

/**
 * Callback for the erase operation done during the format process
 */
static void data_format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Format error...");
		down(&data_config.format_lock);
		data_config.format_done = -1;
		up(&data_config.format_lock);
		return;
	}

	down(&data_config.format_lock);
	data_config.format_done = 1;
	up(&data_config.format_lock);
}

/**
 * Callback for the erase operation done during the format process
 */
static void metadata_format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Format error...");
		down(&meta_config.format_lock);
		meta_config.format_done = -1;
		up(&meta_config.format_lock);
		return;
	}

	down(&meta_config.format_lock);
	meta_config.format_done = 1;
	up(&meta_config.format_lock);
}


/**
 * Format operation: we erase the entire flash partition
 */
static int format_config(lkp_kv_cfg *config,
			 void (*callback)(struct erase_info *e))
{
	struct erase_info ei;
	int i, j;

	/* erasing one or several flash blocks is made through the use of an
	 * erase_info structure passed to the MTD NAND driver */

	ei.mtd = config->mtd;
	ei.len = ((uint64_t) config->block_size) *
		((uint64_t) config->nb_blocks);
	ei.addr = 0x0;
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done */
	ei.callback = callback;

	config->format_done = 0;

	/* Call the MTD driver  */
	if (config->mtd->_erase(config->mtd, &ei) != 0)
		return -1;

	/* on attend la fin effective de l'operation avec un spinlock.
	 * C'est la fonction callback qui mettra format_done a 1 */
	/* TODO change to a condwait here */
	while (1)
		if (!down_trylock(&config->format_lock)) {
			if (config->format_done) {
				up(&config->format_lock);
				break;
			}
			up(&config->format_lock);
		}

	/* was there a driver issue related to the erase oepration? */
	if (config->format_done == -1)
		return -1;

	/* update metadata: now all flash blocks and pages are free */
	for (i = 0; i < config->nb_blocks; i++) {
		config->blocks[i].state = BLK_FREE;
		for (j = 0; j < config->pages_per_block; j++)
			config->blocks[i].pages_states[j] = PG_FREE;
	}

	config->current_block = 0;
	config->current_page_offset = 0;
	config->blocks[0].state = BLK_USED;
	config->read_only = 0;

	printk(PRINT_PREF "Format done\n");

	return 0;
}

int format(void)
{
	int ret = 0;

	ret = format_config(&data_config, data_format_callback);

	if (ret != 0) {
		printk(PRINT_PREF "Format data partition failed\n");
		return ret;
	}

	ret = format_config(&meta_config, metadata_format_callback);

	if (ret != 0) {
		printk(PRINT_PREF "Format meta-data partition failed\n");
		return ret;
	}

	return ret;
}



/* Setup init and exit functions */
module_init(lkp_kv_init);
module_exit(lkp_kv_exit);

/**
 * General Info about the module
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Chauhan <zxcve@vt.edu>");
MODULE_DESCRIPTION("LKP key-value store prototype");
