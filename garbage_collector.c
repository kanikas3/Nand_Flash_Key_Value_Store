/*
 * Defines the garbage collection routines
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

/* Number of pages written on the flash */
uint64_t total_written_page = 0;

/* Jiffies for controlling the garbage collector */
unsigned long old_jiffies = 0;

#define PRINT_PREF KERN_INFO "GARBAGE_COLLECTOR "

/**
 * @brief Reclaims all the vpage marked as invalid
 *
 * @param ppage Start page of the block to be reclaimed
 */
static void project6_reclaim_pages(uint64_t ppage)
{
	uint64_t j,k;
	uint8_t status;

	for (k = ppage; k < ppage +
	     data_config.pages_per_block; k++) {

		status = project6_get_ppage_state(k);

		project6_set_ppage_state(k, PAGE_FREE);

		if (status == PAGE_INVALID) {
			for (j = 0;
			     j < data_config.nb_blocks *
			     data_config.pages_per_block;
			     j++) {

				/*Reverse lookup for ppage-> vpage for invalid
				 * pages */

				if (mapper[j] == k) {
					mapper[j] =
						PAGE_GARBAGE_RECLAIMED;
					total_written_page--;
					break;
					printk(PRINT_PREF "Reclaim vpage %llu\n"
					       , j);
				}
			}
		}
	}

}

/**
 * @brief Migrate a given block to another free block
 *
 * @param block_num The block number to be migrated
 *
 * @return Appropriate error codes, 0 for success
 */
static int project6_migrate_block(uint64_t block_num)
{
	/*NOTE: The migration can fail in middle, if there are not enough free
	 * blocks, this is by design.
	 */
	int num_pages = data_config.pages_per_block;
	uint64_t i = 0;
	uint64_t j = 0;
	uint64_t ppage = block_num * data_config.pages_per_block;
	uint64_t npage;
	int ret;
	uint8_t status;

	while (j < num_pages) {
		status = project6_get_ppage_state(ppage);

		if (status == PAGE_VALID) {

			for (i = 0; i < data_config.nb_blocks *
					data_config.pages_per_block; i++) {
				if (mapper[i] == ppage) {
					ret = project6_create_mapping_new_block(
							i,
							&npage, block_num);

					if (ret < 0) {
						printk(PRINT_PREF "Creating mapping for migration failed\n");
						return ret;
					}
					/* Incremented by create mapping, hence
					 * reduce */
					total_written_page--;
					break;
				}
			}

			ret = read_page(ppage, page_buffer, &data_config);

			if (ret < 0) {
				printk(PRINT_PREF "Reading page for migration failed\n");
				return ret;
			}

			ret = write_page(npage, page_buffer, &data_config);

			if (ret < 0) {
				printk(PRINT_PREF "Writing page for migration failed\n");
				return ret;
			}

			printk("Moved page %llu to page %llu\n", ppage, npage);

			project6_set_ppage_state(ppage, PAGE_INVALID);

		}

		j++;
		ppage++;
	}

	return 0;
}

/**
 * @brief Starts Garbage Collection
 *
 * @param threshold TotalPages/Threshold is the marker when garbage collection
 * starts
 *
 * @return Appropriate Error code, 0 on success
 */
int project6_garbage_collection(int threshold)
{
	uint64_t ppage = 0;
	uint8_t status;
	uint64_t num_pages = data_config.nb_blocks *
		data_config.pages_per_block;
	int invalid_page_counter = 0;
	int page_per_block_counter = 0;
	uint64_t block_counter = 0;
	int ret;
/*
	if (old_jiffies == 0)
		old_jiffies = jiffies;
	else if (time_before(jiffies, old_jiffies + 1 * HZ / 3))
		return 0;
	else
		old_jiffies = jiffies;
*/
	printk(PRINT_PREF "Starting Garbage Collection \n");
	while (ppage < num_pages) {

		status = project6_get_ppage_state(ppage);

		if (status == PAGE_INVALID)
			invalid_page_counter++;

		ppage++;

		page_per_block_counter++;

		if (page_per_block_counter == data_config.pages_per_block) {

			if (invalid_page_counter >= data_config.pages_per_block
								/ threshold) {
				printk(PRINT_PREF "Migration for %llu block\n",
				       block_counter);
				ret = project6_migrate_block(block_counter);

				if (ret) {
					printk(PRINT_PREF "Migration of block %llu for garbage collection failed \n", block_counter);
					return ret;
				}

				ret = erase_block(block_counter, 1,
						&data_config,
						data_format_callback);

				if (ret) {

					printk(PRINT_PREF "erase block %llu for garbage collection failed \n", block_counter);
					return ret;
				}

				project6_reclaim_pages(block_counter *
					      data_config.pages_per_block);

			}
			block_counter++;
			page_per_block_counter = 0;
			invalid_page_counter = 0;
		}
	}

	return 0;
}

