/*
 * Defines the page manager routines
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

#define PRINT_PREF KERN_INFO "PAGE_MANAGER"

static uint64_t current_free_page = 0xDEADBEEF;


/**
 * @brief Finds a free page from the given page
 *
 * @param ppage Offset from which next free page is found
 */
void project6_fix_free_page_pointer(uint64_t ppage)
{
	uint64_t i = 0;
	uint8_t status;
	uint64_t num_pages = data_config.nb_blocks *
		data_config.pages_per_block;

	/* To wrap around */
	if (ppage >= num_pages)
		ppage = 0;

	while (i < num_pages) {

		status = project6_get_ppage_state(ppage);

		if (status == PAGE_FREE) {
			/* If back to previous free page,
			 * then we dont have any free page */
			if (ppage == current_free_page
			    && current_free_page)
				break;
			current_free_page = ppage;
			return;
		}

		ppage = (ppage + 1) % num_pages;

		i++;
	}
	/* Move to read only mode if no free page */
	data_config.read_only = 1;
}


/**
 * @brief Give a free page to perform write
 *
 * @param ppage Pointer where the free page is returned
 *
 * @return 0 on success, -ENOMEM on failure
 */
static int get_free_page(uint64_t *ppage)
{
	if (data_config.read_only) {
		printk(PRINT_PREF "No free pages to give \n");
		return -ENOMEM;
	}

	*ppage = current_free_page;

	project6_fix_free_page_pointer(current_free_page+1);

	return 0;
}


/**
 * @brief Get existing state for the physical page
 *
 * @param ppage Physical page number
 *
 * @return state of the page
 */
uint8_t project6_get_ppage_state(uint64_t ppage)
{
	uint64_t offset = ppage / 4;
	int index = ppage % 4;

	return (bitmap[offset] >> (index * 2)) & 0x3;
}


/**
 * @brief Set the state for the physical page
 *
 * @param ppage Physical page number
 * @param state State to be set
 */
void project6_set_ppage_state(uint64_t ppage, uint8_t state)
{
	uint64_t offset = ppage / 4;
	int index = ppage % 4;

	bitmap[offset] = (bitmap[offset] & ~(0x3 << index * 2)) |
			((state & 0x3) << index * 2);
}

/**
 * @brief Creates a mapping for a given vpage
 *
 * @param vpage Virtual page to be mapped
 * @param ppage Physical page which is returned
 *
 * @return 0 on success, otherwise appropriate error codes
 */
static int create_mapping(uint64_t vpage, uint64_t *ppage)
{
	int ret = get_free_page(ppage);

	if (ret) {
		printk(PRINT_PREF "could not create mapping\n");
		return ret;
	}

	mapper[vpage] = *ppage;

	project6_set_ppage_state(*ppage, PAGE_VALID);

	total_written_page++;

	return 0;
}



/**
 * @brief Creates a mapping in a block different than given block, it is used by
 * the garbage collection to find a mapping in the new block
 *
 * @param vpage vpage for which we need mapping
 * @param ppage Returns the physical page into this pointer
 * @param blk_number Block number which must be avoid while providing mapping
 *
 * @return 0 on success, otherwise appropriate error code
 */
int project6_create_mapping_new_block(uint64_t vpage, uint64_t *ppage,
			     uint64_t blk_number)
{
	int ret = get_free_page(ppage);

	if (ret) {
		printk(PRINT_PREF "could not create mapping due to no free page\n");
		return ret;
	}

	/* iterate till we reach a new block */
	while (*ppage >= blk_number * data_config.pages_per_block && *ppage <
	       (blk_number + 1) * data_config.pages_per_block) {

		ret = get_free_page(ppage);
		if (ret != 0) {
			printk(PRINT_PREF "could not create mapping due to no free page\n");
			return ret;
		}

	}

	mapper[vpage] = *ppage;

	project6_set_ppage_state(*ppage, PAGE_VALID);

	total_written_page++;

	return 0;
}

/**
 * @brief Create mapping for multiple pages
 *
 * @param vpage Virtual page to be mapped
 * @param num_pages Number of pages to be mapped
 *
 * @return 0 on success, otherwise appropriate error code
 */
int project6_create_mapping_multipage(uint64_t vpage, uint32_t num_pages)
{
	uint32_t page = 0;
	uint64_t lpage = vpage;
	uint64_t ppage;

	while (page < num_pages) {
		if (mapper[lpage] != PAGE_UNALLOCATED &&
		    mapper[lpage] != PAGE_GARBAGE_RECLAIMED) {
	//		printk("multipage mapping not allowed for %llu \n",
	//		       lpage);
			return -EPERM;
		}
		if (lpage == data_config.nb_blocks *
				data_config.pages_per_block)
			return -EPERM;

	//	mapper[lpage] = PAGE_GARBAGE_RECLAIMED;
		lpage++;
		page++;
	}

	lpage = vpage;
	page = 0;

	while (page < num_pages) {
		if (create_mapping(lpage, &ppage)) {
			printk("mapping failed for %llu \n", lpage);
			return -ENOMEM;
		}
		lpage++;
		page++;
	}

	return 0;
}



/**
 * @brief Gets  the existing mapping for the given vpage
 *
 * @param vpage Vpage whose mapping is found
 * @param ppage Ppage pointer to be filled
 *
 * @return Appropriate codes based on state of vpage
 */
int project6_get_existing_mapping(uint64_t vpage, uint64_t *ppage)
{
	if (vpage >= data_config.nb_blocks * data_config.pages_per_block)
		return PAGE_NOT_MAPPED;

	*ppage = mapper[vpage];

	if (*ppage == PAGE_UNALLOCATED)
		return PAGE_NOT_MAPPED;

	if (*ppage == PAGE_GARBAGE_RECLAIMED)
		return PAGE_RECLAIMED;

	return project6_get_ppage_state(*ppage);
}

/**
 * @brief Marks the vpage invalid
 *
 * @param vpage Vpage to be marked invalid
 * @param num_pages Number of pages to be marked invalid
 *
 * @return 0 on success, otherwise -EPERM
 */
int project6_mark_vpage_invalid(uint64_t vpage, uint64_t num_pages)
{
	int state;
	uint64_t i = 0;
	uint64_t ppage;

	while (i < num_pages) {

		state = project6_get_existing_mapping(vpage + i, &ppage);

		if (state != PAGE_VALID) {
			printk(PRINT_PREF "Trying to mark a non-valid page as invalid\n");
			return -EPERM;
		}
		i++;

		project6_set_ppage_state(ppage, PAGE_INVALID);

	}

	return 0;
}

