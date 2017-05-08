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

/* prototypes */
int init_config(int mtd_index);
void destroy_config(void);
void print_config(void);
int write_page(int page_index, const char *buf);
int read_page(int page_index, char *buf);
void format_callback(struct erase_info *e);
int get_next_page_index_to_write(void);
int get_next_free_block(void);
int init_scan(void);

lkp_kv_cfg config;

/* The module takes one parameter which is the index of the target flash
 * partition */
int MTD_INDEX = -1;
module_param(MTD_INDEX, int, 0);
MODULE_PARM_DESC(MTD_INDEX, "Index of target mtd partition");

/**
 * Module initialization function
 */
static int __init lkp_kv_init(void)
{
	ktime_t kt, start_kt, stop_kt;

	printk(PRINT_PREF "Loading... \n");

	start_kt = ktime_get();

	if (init_config(MTD_INDEX) != 0) {
		printk(PRINT_PREF "Initialization error\n");
		return -1;
	}

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -1;
	}

	stop_kt = ktime_get();

	kt = ktime_sub(stop_kt, start_kt);

	printk(PRINT_PREF "Mount time: %llu usecs\n", (kt.tv64)/1000);

	return 0;
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	printk(PRINT_PREF "Exiting ... \n");
	device_exit();
	destroy_config();
}

/**
 * Global state initialization, return 0 when ok, -1 on error
 */
int init_config(int mtd_index)
{
	uint64_t tmp_blk_num;

	if (mtd_index == -1) {
		printk(PRINT_PREF
		       "Error, flash partition index missing, should be"
		       " indicated for example like this: MTD_INDEX=5\n");
		return -1;
	}

	config.format_done = 0;
	config.read_only = 0;

	config.mtd_index = mtd_index;

	/* The flash partition is manipulated by caling the driver, through the
	 * mtd_info object. There is one of these object per flash partition */
	config.mtd = get_mtd_device(NULL, mtd_index);

	if (config.mtd == NULL)
		return -1;

	config.block_size = config.mtd->erasesize;
	config.page_size = config.mtd->writesize;
	config.pages_per_block = config.block_size / config.page_size;

	tmp_blk_num = config.mtd->size;
	do_div(tmp_blk_num, (uint64_t) config.mtd->erasesize);
	config.nb_blocks = (int)tmp_blk_num;

	/* Semaphore initialized to 1 (available) */
	sema_init(&config.format_lock, 1);

	print_config();

	/* Flash scan for metadata creation: which flash blocks and pages are 
	 * free/occupied */
	if (init_scan() != 0) {
		printk(PRINT_PREF "Init scan error\n");
		return -1;
	}

	return 0;
}

/**
 * Launch time metadata creation: flash is scanned to determine which flash 
 * blocs and pages are free/occupied. Return 0 when ok, -1 on error
 */
int init_scan()
{
	int i, j;
	char *buffer;

	buffer = (char *)vmalloc(config.page_size * sizeof(char));

	/* metadata initialization */
	config.blocks =
	    (blk_info *) vmalloc((config.nb_blocks) * sizeof(blk_info));
	for (i = 0; i < config.nb_blocks; i++) {
		config.blocks[i].state = BLK_FREE;
		config.blocks[i].pages_states =
		    (page_state *) vmalloc(config.pages_per_block *
					   sizeof(page_state));
		for (j = 0; j < config.pages_per_block; j++)
			config.blocks[i].pages_states[j] = PG_FREE;
	}

	/* scan: each flash page is read sequentially */
	for (i = 0; i < config.nb_blocks; i++) {
		for (j = 0; j < config.pages_per_block; j++) {
			int key_len;
			if (read_page(i * config.pages_per_block + j, buffer) !=
			    0)
				goto err_free;
			memcpy(&key_len, buffer, sizeof(int));

			/* If there are only ones at the beginning of the flash page, the page
			 * is free */
			if (key_len == 0xFFFFFFFF)
				break;
			else {
				/* otherwise the page contains something */
				config.blocks[i].state = BLK_USED;
				config.blocks[i].pages_states[j] = PG_VALID;
				/* If all pages contain something: switch to read-only mode */
				if ((i == (config.nb_blocks - 1))
				    && (j == (config.pages_per_block - 1)))
					config.read_only = 1;
			}
		}
	}

	vfree(buffer);

	return 0;

err_free:
	vfree(buffer);
	for (i = 0; i < config.nb_blocks; i++)
		vfree(config.blocks[i].pages_states);
	vfree(config.blocks);
	return -1;
}

/**
 * Freeing stuff on exit
 */
void destroy_config(void)
{
	int i;
	put_mtd_device(config.mtd);
	for (i = 0; i < config.nb_blocks; i++)
		vfree(config.blocks[i].pages_states);
	vfree(config.blocks);
}

/**
 * Adding a key-value couple. Returns -1 when ok and a negative value on error:
 * -1 when the size to write is too big
 * -2 when the key already exists
 * -3 when we are in read-only mode
 * -4 when the MTD driver returns an error
 */
int set_keyval(const char *key, const char *val)
{
	int key_len, val_len, i, ret;
	char *buffer;

	key_len = strlen(key);
	val_len = strlen(val);

	if ((key_len + val_len + 2 * sizeof(int)) > config.page_size) {
		/* size to write is too big */
		return -1;
	}

	/* the buffer that we are going to write on flash */
	buffer = (char *)vmalloc(config.page_size * sizeof(char));

	/* if the key already exists, return without writing anything to flash */
	ret = get_keyval(key, buffer);
	if (ret >= 0) {
		printk(PRINT_PREF "Key \"%s\" already exists in page %d\n", key,
		       ret);
		vfree(buffer);
		return -2;
	}

	/* prepare the buffer we are going to write on flash */
	for (i = 0; i < config.page_size; i++)
		buffer[i] = 0x0;

	/* key size ... */
	memcpy(buffer, &key_len, sizeof(int));
	/* ... value size ... */
	memcpy(buffer + sizeof(int), &val_len, sizeof(int));
	/* ... the key itself ... */
	memcpy(buffer + 2 * sizeof(int), key, key_len);
	/* ... then the value itself. */
	memcpy(buffer + 2 * sizeof(int) + key_len, val, val_len);

	/* actual write on flash */
	ret =
	    write_page(config.current_block * config.pages_per_block +
		       config.current_page_offset, buffer);

	vfree(buffer);

	if (ret == -1)		/* read-only */
		return -3;
	else if (ret == -2)	/* write error */
		return -4;

	return 0;
}

/**
 * Getting a value from a key.
 * Returns the index of the page containing the key/value couple on success,
 * and a negative number on error:
 * -1 when the key is not found
 * -2 on MTD read error
 */
int get_keyval(const char *key, char *val)
{
	int i, j;
	char *buffer;

	buffer = (char *)vmalloc(config.page_size * sizeof(char));

	/* read the entirety of valid flash pages until we found the requested key */
	for (i = 0; i < config.nb_blocks; i++)
		if (config.blocks[i].state == BLK_USED)
			for (j = 0; j < config.pages_per_block; j++)
				if (config.blocks[i].pages_states[j] ==
				    PG_VALID) {
					int key_len, val_len;
					char *cur_key, *cur_val;

					/* flash read */
					if (read_page
					    (i * config.pages_per_block + j,
					     buffer) != 0) {
						vfree(buffer);
						return -2;
					}

					/* get the key and value */
					memcpy(&key_len, buffer, sizeof(int));
					memcpy(&val_len, buffer + sizeof(int),
					       sizeof(int));

					if (key_len != 0xFFFFFFFF) {	/* shoud always be true */
						cur_key =
						    buffer + 2 * sizeof(int);
						cur_val =
						    buffer + 2 * sizeof(int) +
						    key_len;
						if (!strncmp
						    (cur_key, key,
						     strlen(key))) {
							/* key found */
							memcpy(val, cur_val,
							       val_len);
							val[val_len] = '\0';
							vfree(buffer);
							return i *
							    config.
							    pages_per_block + j;
						}
					}
				}

	/* key not found */
	vfree(buffer);
	return -1;
}

/**
 * After an insertion, determine which is the flash page that will receive the 
 * next insertion. Return the correspondign flash page index, or -1 if the 
 * flash is full
 */
int get_next_page_index_to_write()
{
	/* in general we want the next flash page in the block */
	config.current_page_offset++;

	/* but sometimes we need to jump to the next flash block */
	if (config.current_page_offset == config.block_size / config.page_size) {
		config.current_block = get_next_free_block();

		/* flash full */
		if (config.current_block == -1)
			return -1;

		config.blocks[config.current_block].state = BLK_USED;
		config.current_page_offset = 0;
	}
	return config.current_page_offset;
}

/**
 * When a flash block is full, we choose the next one through this function. 
 * Return the next block index, or -1 on error
 */
int get_next_free_block()
{
	int i;

	for (i = 0; i < config.nb_blocks; i++)
		if (config.blocks[i].state == BLK_FREE)
			return i;

	/* If we get there, no free block left... */

	return -1;
}

/**
 * Callback for the erase operation done during the format process
 */
void format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Format error...");
		down(&config.format_lock);
		config.format_done = -1;
		up(&config.format_lock);
		return;
	}

	down(&config.format_lock);
	config.format_done = 1;
	up(&config.format_lock);
}

/**
 * Format operation: we erase the entire flash partition
 */
int format()
{
	struct erase_info ei;
	int i, j;

	/* erasing one or several flash blocks is made through the use of an 
	 * erase_info structure passed to the MTD NAND driver */
	ei.mtd = config.mtd;
	ei.len = ((uint64_t) config.block_size) * ((uint64_t) config.nb_blocks);
	ei.addr = 0x0;
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done */
	ei.callback = format_callback;

	config.format_done = 0;

	/* Call the MTD driver  */
	if (config.mtd->_erase(config.mtd, &ei) != 0)
		return -1;

	/* on attend la fin effective de l'operation avec un spinlock. 
	 * C'est la fonction callback qui mettra format_done a 1 */
	/* TODO change to a condwait here */
	while (1)
		if (!down_trylock(&config.format_lock)) {
			if (config.format_done) {
				up(&config.format_lock);
				break;
			}
			up(&config.format_lock);
		}

	/* was there a driver issue related to the erase oepration? */
	if (config.format_done == -1)
		return -1;

	/* update metadata: now all flash blocks and pages are free */
	for (i = 0; i < config.nb_blocks; i++) {
		config.blocks[i].state = BLK_FREE;
		for (j = 0; j < config.pages_per_block; j++)
			config.blocks[i].pages_states[j] = PG_FREE;
	}

	config.current_block = 0;
	config.current_page_offset = 0;
	config.blocks[0].state = BLK_USED;
	config.read_only = 0;

	printk(PRINT_PREF "Format done\n");

	return 0;
}

/**
 * Write the flash page with index page_index, data to write is in buf. 
 * Returns:
 * 0 on success
 * -1 if we are in read-only mode
 * -2 when a write error occurs
 */
int write_page(int page_index, const char *buf)
{
	uint64_t addr;
	size_t retlen;

	/* if the flash partition is full, dont write */
	if (config.read_only)
		return -1;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);

	/* call the NAND driver MTD to perform the write operation */
	if (config.mtd->
	    _write(config.mtd, addr, config.page_size, &retlen, buf) != 0)
		return -2;

	/* update metadata to set the written page state, it is now valid */
	config.blocks[page_index /
		      config.pages_per_block].pages_states[page_index %
							   config.
							   pages_per_block] =
	    PG_VALID;

	/* set the flash page that will serve the next write oepration.
	 * if the flash partition is full, switch to read-only mode */
	if (get_next_page_index_to_write() == -1) {
		printk(PRINT_PREF
		       "no free block left... switching to read-only mode\n");
		config.read_only = 1;
		return -1;
	}

	return 0;
}

/**
 * Read the flash page with index page_index, data read are placed in buf
 * Retourne 0 when ok, something else on error
 */
int read_page(int page_index, char *buf)
{
	uint64_t addr;
	size_t retlen;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config.page_size);

	/* call the NAND driver MTD to perform the read operation */
	return config.mtd->_read(config.mtd, addr, config.page_size, &retlen,
				 buf);
}

/**
 * Print some statistics on the kernel log
 */
void print_config()
{
	printk(PRINT_PREF "Config : \n");
	printk(PRINT_PREF "=========\n");

	printk(PRINT_PREF "mtd_index: %d\n", config.mtd_index);
	printk(PRINT_PREF "nb_blocks: %d\n", config.nb_blocks);
	printk(PRINT_PREF "block_size: %d\n", config.block_size);
	printk(PRINT_PREF "page_size: %d\n", config.page_size);
	printk(PRINT_PREF "pages_per_block: %d\n", config.pages_per_block);
	printk(PRINT_PREF "read_only: %d\n", config.read_only);
}

/* Setup init and exit functions */
module_init(lkp_kv_init);
module_exit(lkp_kv_exit);

/**
 * Infos generale sur le module
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pierre Olivier <polivier@vt.edu>");
MODULE_DESCRIPTION("LKP key-value store prototype");
