/*
 * Defines the meta-data construction and update routines
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

uint8_t *bitmap = NULL;
uint64_t *mapper = NULL;

static uint64_t bitmap_start = 0x1;
static uint64_t bitmap_pages;

static uint64_t mapper_start;
static uint64_t mapper_pages;


#define PRINT_PREF KERN_INFO "META-DATA "

/**
 * @brief Creates a new metadata from scratch
 *
 * @param meta_config Configuration of the meta-data
 *
 * @return returns 0 on success, otherwise appropriate error code
 */
int project6_create_meta_data(project6_cfg *meta_config)
{
	uint32_t *signature = (uint32_t *)page_buffer;
	size_t i;
	int ret;

	for (i = 0; i < meta_config->page_size; i++)
		page_buffer[i] = 0xFF;

	*signature = 0xdeadbeef;

	*(signature+4) = total_written_page;

	ret = write_page(0, page_buffer, meta_config);

	if (ret) {
		printk(PRINT_PREF "Writing page for signature failed\n");
		return ret;
	}

	return 0;
}

/**
 * @brief Construct the in memory meta-data
 *
 * @param meta_config Config Pointer of meta-data partition
 * @param data_config Config Pointer of data partition
 * @param read_disk Read from disk or perform default meta-data construction
 *
 * @return 0 for success, otherwise appropriate error code
 */
int project6_construct_meta_data(project6_cfg *meta_config,
			project6_cfg *data_config,
			bool read_disk)
{
	uint32_t *signature = (uint32_t *)page_buffer;
	uint64_t bitmap_size = data_config->nb_blocks *
		data_config->pages_per_block;
	uint64_t bitmap_bytes;

	uint64_t mapper_bytes = data_config->nb_blocks *
		data_config->pages_per_block * sizeof(uint64_t);
	size_t i = 0;
	size_t j = 0;
	int ret;

	uint8_t *byte_mapper;

	if (read_disk == true) {
		ret = read_page(0, page_buffer, meta_config);
		if (ret) {
			printk(PRINT_PREF "Read for constructing meta-data failed\n");
			return ret;
		}

		if (*signature != 0xdeadbeef) {
			printk(PRINT_PREF "You must format the flash before usage\n");
			return -1;
		}

		total_written_page = *(signature + 4);
	}

	if (bitmap_size % 4 == 0)
		bitmap_bytes = bitmap_size / 4;
	else
		bitmap_bytes = bitmap_size / 4 + 1;

	if (bitmap_bytes % meta_config->page_size == 0)
		bitmap_pages = bitmap_bytes / meta_config->page_size;
	else
		bitmap_pages = bitmap_bytes / meta_config->page_size + 1;


	if (bitmap_start + bitmap_pages > meta_config->nb_blocks *
	    meta_config->pages_per_block) {

		printk(PRINT_PREF " Not enough pages for bitmap in meta partition\n");
		return -1;
	}

	bitmap = (uint8_t *) kmalloc(bitmap_pages * meta_config->page_size,
				     GFP_KERNEL);

	if (bitmap == NULL) {
		printk(PRINT_PREF "kmalloc failed for bitmap allocation\n");
		return -1;
	}

	for (i = bitmap_start; i < bitmap_start + bitmap_pages ; i++) {
		if (read_disk) {
			if (read_page(i, bitmap + (j) * meta_config->page_size,
				      meta_config) != 0) {
				printk(PRINT_PREF
				       "Read for %lu page failed\n", i);
				return -1;
			}
			j++;
		} else {
			memset(bitmap, 0xFF,
			       bitmap_pages * meta_config->page_size);
		}
	}

	mapper_start = bitmap_pages + bitmap_start + 1;

	if (mapper_bytes % meta_config->page_size == 0)
		mapper_pages = mapper_bytes / meta_config->page_size;
	else
		mapper_pages = mapper_bytes / meta_config->page_size + 1;

	if (mapper_start + mapper_pages > meta_config->nb_blocks *
	    meta_config->pages_per_block) {

		printk(PRINT_PREF " Not enough pages for mapper in meta partition\n");
		return -1;
	}

	mapper = (uint64_t *) kmalloc(mapper_pages * meta_config->page_size,
				      GFP_KERNEL);

	if (mapper == NULL) {
		printk(PRINT_PREF "kmalloc failed for mapper allocation\n");
		return -1;
	}

	byte_mapper = (uint8_t *)mapper;

	j = 0;

	for (i = mapper_start; i < mapper_start + mapper_pages ; i++) {
		if (read_disk) {
			if (read_page(i, byte_mapper + (j) *
				      meta_config->page_size,
				      meta_config) != 0) {
				printk(PRINT_PREF "Read for %lu page failed\n",
				       i);
				return -1;
			}
			j++;
		} else {
			memset(byte_mapper, 0xFF,
			       mapper_pages * meta_config->page_size);
		}
	}

	project6_fix_free_page_pointer(0);

	return 0;
}

/**
 * @brief Flush the meta-data back to flash
 *
 * @param config Config of the meta-data
 */
void project6_flush_meta_data_to_flash(project6_cfg *config)
{
	uint64_t total_pages = mapper_pages + bitmap_pages + 1;
	uint64_t block_count;
	uint8_t *byte_mapper = (uint8_t *)mapper;
	size_t i = 0;
	size_t j = 0;

	if (total_pages % config->pages_per_block)
		block_count = total_pages / config->pages_per_block + 1;
	else
		block_count = total_pages / config->pages_per_block;

	if (erase_block(0, block_count, config, metadata_format_callback)) {
		printk("Erasing the block device failed while flushing\n");
		return;
	}

	project6_create_meta_data(config);

	for (i = bitmap_start; i < bitmap_start + bitmap_pages ; i++) {
		if (write_page(i, bitmap + (j) * config->page_size,
			       config) != 0) {
			printk(PRINT_PREF "Write for %lu page failed\n", i);
		}
		j++;
	}

	j = 0;

	for (i = mapper_start; i < mapper_start + mapper_pages ; i++) {
		if (write_page(i, byte_mapper + (j) * config->page_size,
			       config) != 0) {
			printk(PRINT_PREF "Write for %lu page failed\n", i);
		}
		j++;
	}
	j = 0;

	for (i = mapper_start; i < mapper_start + mapper_pages ; i++) {
		if (read_page(i, byte_mapper + (j) * config->page_size,
			      config) != 0) {
			printk(PRINT_PREF "Write for %lu page failed\n", i);
		}
		j++;
	}
}
