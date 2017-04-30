/*
 * Driver related functionality
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include "core.h"
#include "device.h"
#include "cache.h"

#define PRINT_PREF KERN_INFO "CORE "

/**
 * @brief Configuration for meta-data partition
 */
project6_cfg meta_config;


/**
 * @brief Configuration for data partition
 */
project6_cfg data_config;

/**
 * @brief Page buffer for writing/reading page
 */
uint8_t *page_buffer = NULL;


/**
 * @brief Destroys the config
 *
 * @param config Pointer to the config
 */
static void destroy_config(project6_cfg *config)
{
	put_mtd_device(config->mtd);
}


/**
 * @brief Prints the given config
 *
 * @param config Pointer to the config
 */
static void print_config(project6_cfg *config)
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
 * @brief Initializes the given config
 *
 * @param mtd_index Partition index for the flash
 * @param config Pointer to the config
 *
 * @return 0 for success, -1 for failure
 */
static int init_config(int mtd_index, project6_cfg *config)
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

	return 0;
}


/**
 * @brief Reads a page from the flash
 *
 * @param page_index Index of the page
 * @param buf Buffer where we need to read
 * @param config Config for the partition
 *
 * @return 0 for success, otherwise appropriate error code
 */
int read_page(int page_index, char *buf, project6_cfg *config)
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
 * @brief Writes a page to the flash
 *
 * @param page_index Index of the page
 * @param buf Buffer where we need to read
 * @param config Config for the partition
 *
 * @return 0 for success, otherwise appropriate error code
 */
int write_page(int page_index, const char *buf, project6_cfg *config)
{
	uint64_t addr;
	size_t retlen;

	/* compute the flash target address in bytes */
	addr = ((uint64_t) page_index) * ((uint64_t) config->page_size);

	/* call the NAND driver MTD to perform the write operation */
	return config->mtd->_write(config->mtd, addr,
				   config->page_size, &retlen, buf);
}

/**
 * @brief Callback for datapartition erase operation
 *
 * @param e Pointer to erase info structure
 */
void data_format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "Data Partition Format error...");
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
 * @brief Callback for metadata partition erase operation
 *
 * @param e Pointer to erase info structure
 */
void metadata_format_callback(struct erase_info *e)
{
	if (e->state != MTD_ERASE_DONE) {
		printk(PRINT_PREF "MetaData Partition Format error...");
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
 * @brief Performs erase of the given block
 *
 * @param block_index Index of the block to erase
 * @param block_count Number of blocks to be erased
 * @param config Config of the partition
 * @param callback Callback to be called for erase
 *
 * @return -1 for failure, 0 for success
 */
int erase_block(uint64_t block_index, int block_count,
		project6_cfg *config, void (*callback)(struct erase_info *e))
{
	struct erase_info ei;

	/* erasing one or several flash blocks is made through the use of an
	 * erase_info structure passed to the MTD NAND driver */

	ei.mtd = config->mtd;
	ei.len = ((uint64_t) config->block_size) * block_count;
	ei.addr = block_index * config->pages_per_block * config->page_size;
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done */
	ei.callback = callback;

	config->format_done = 0;

	/* Call the MTD driver  */
	if (config->mtd->_erase(config->mtd, &ei) != 0)
		return -1;

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

	return 0;
}


/**
 * @brief Formats the entire partition
 *
 * @param config Config of the partition to be erased
 * @param callback Callback to be called while erase
 *
 * @return 0 for success, otherwise appropriate error code
 */
static int format_config(project6_cfg *config,
			 void (*callback)(struct erase_info *e))
{
	int ret;

	ret = erase_block(0, config->nb_blocks, config, callback);

	if (ret != 0) {
		printk(PRINT_PREF "Format failed \n");
		return ret;
	}

	config->read_only = 0;

	printk(PRINT_PREF "Format done\n");

	return 0;
}

/**
 * @brief Performs format of the disk
 *
 * @return 0 for success, otherwise appropriate error codes
 */
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

	total_written_page = 0;

	ret = project6_create_meta_data(&meta_config);

	if (ret != 0) {
		printk(PRINT_PREF "Creating metadata failed\n");
		return ret;
	}

	ret = project6_construct_meta_data(&meta_config, &data_config, false);

	if (ret != 0) {
		printk(PRINT_PREF "Constructing metadata failed\n");
		return ret;
	}

	project6_cache_clean();

	return ret;
}

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

	page_buffer = (uint8_t *)kmalloc(data_config.page_size, GFP_KERNEL);

	if (page_buffer == NULL) {
		printk(PRINT_PREF "Page buffer allocation failed\n");
		return -ENOMEM;
	}

	project6_construct_meta_data(&meta_config, &data_config, true);

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

	project6_flush_meta_data_to_flash(&meta_config);

	project6_cache_clean();

	device_exit();

	destroy_config(&meta_config);
	destroy_config(&data_config);

	if (page_buffer)
		kfree(page_buffer);

	if (bitmap)
		kfree(bitmap);

	if (mapper)
		kfree(mapper);
}

/* Setup init and exit functions */
module_init(lkp_kv_init);
module_exit(lkp_kv_exit);

/**
 * General Info about the module
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Chauhan <zxcve@vt.edu>");
MODULE_DESCRIPTION("Project6 key-value store");
