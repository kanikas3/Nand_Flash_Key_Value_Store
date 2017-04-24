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

lkp_kv_cfg meta_config;
lkp_kv_cfg data_config;
uint8_t *page_buffer = NULL;
uint8_t *bitmap = NULL;
uint64_t *mapper = NULL;

uint64_t bitmap_start = 0x1;
uint64_t bitmap_pages;

uint64_t mapper_start;
uint64_t mapper_pages;

uint64_t current_free_page;

uint64_t total_written_page = 0;

static int init_config(int mtd_index, lkp_kv_cfg *config);
static void print_config(lkp_kv_cfg *config);
static void print_config(lkp_kv_cfg *config);
int read_page(int page_index, char *buf, lkp_kv_cfg *config);
int write_page(int page_index, const char *buf, lkp_kv_cfg *config);
static void data_format_callback(struct erase_info *e);
static void metadata_format_callback(struct erase_info *e);
static int format_config(lkp_kv_cfg *config,
			 void (*callback)(struct erase_info *e));
static void destroy_config(lkp_kv_cfg *config);
static int construct_meta_data(lkp_kv_cfg *meta_config,
			       lkp_kv_cfg *data_config,
				bool read_disk);
void fix_free_page_pointer(int ppage);

int migrate(uint64_t block_counter);

int get_free_page(uint64_t *ppage);

static int create_meta_data(lkp_kv_cfg *meta_config);
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

	page_buffer = (uint8_t *)vmalloc(data_config.page_size);

	if (page_buffer == NULL) {
		printk(PRINT_PREF "Page buffer allocation failed\n");
		return -ENOMEM;
	}

	construct_meta_data(&meta_config, &data_config, true);

	if (device_init() != 0) {
		printk(PRINT_PREF "Virtual device creation error\n");
		return -1;
	}

	return 0;
}

static int construct_meta_data(lkp_kv_cfg *meta_config,
			       lkp_kv_cfg *data_config,
				bool read_disk)
{
	uint32_t *signature = (uint32_t *)page_buffer;
	uint64_t bitmap_size = data_config->nb_blocks *
					data_config->pages_per_block;
	uint64_t bitmap_bytes;
	uint64_t bitmap_pages;

	uint64_t mapper_bytes = data_config->nb_blocks *
					data_config->pages_per_block * sizeof(uint64_t);
	uint64_t mapper_pages;

	size_t i;
	size_t j = 0;

	uint8_t *byte_mapper;

	if (read_disk == true) {
		if (read_page(0, page_buffer, meta_config) != 0) {
			printk(PRINT_PREF "Read for constructing meta-data failed\n");
			return -1;
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

	bitmap = (uint8_t *) vmalloc(bitmap_pages * meta_config->page_size);

	if (bitmap == NULL) {
		printk(PRINT_PREF "vmalloc failed for bitmap allocation\n");
		return -1;
	}

	for (i = bitmap_start; i <= bitmap_pages ; i++) {
		if (read_disk) {
			if (read_page(i, bitmap + (j) * meta_config->page_size,
				      meta_config) != 0) {
				printk(PRINT_PREF "Read for %lu page failed\n", i);
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

	mapper = (uint64_t *) vmalloc(mapper_pages * meta_config->page_size);

	if (mapper == NULL) {
		printk(PRINT_PREF "vmalloc failed for mapper allocation\n");
		return -1;
	}

	byte_mapper = (uint8_t *)mapper;

	j = 0;

	for (i = mapper_start; i <= mapper_pages ; i++) {
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

	fix_free_page_pointer(0);


	printk(PRINT_PREF "mapper start %llu \n", mapper_start);
	printk(PRINT_PREF "mapper end %llu \n", mapper_start + mapper_pages);
	printk(PRINT_PREF "bitmap end %llu \n", bitmap_start + bitmap_pages);
	printk(PRINT_PREF "curr pointer %llu \n", current_free_page);

	return 0;
}

void fix_free_page_pointer(int ppage)
{
	uint64_t i = 0;
	uint64_t offset = ppage / 4;
	int index = ppage % 4;
	uint8_t status;
	uint64_t num_pages = data_config.nb_blocks *
		data_config.pages_per_block;

	while (i < num_pages) {

		status = (bitmap[offset] >> (index * 2)) & 0x3;

		if (status == PAGE_FREE) {
			current_free_page = offset * 4 + index;
			return;
		}
		index++;

		if (index == 4) {
			offset++;
			if (offset == num_pages / 4)
				offset = 0;
			index=0;
		}
		i++;
	}

	data_config.read_only = 1;
}

int migrate(uint64_t block_counter)
{
	int num_pages = data_config.pages_per_block;
	uint64_t i = 0;
	int j = 0;
	uint64_t ppage = block_counter * data_config.pages_per_block;
	uint64_t offset = ppage / 4;
	int index =  ppage % 4;
	uint64_t npage;
	int ret;
	uint8_t status;

	while (j < num_pages) {
		status = (bitmap[offset] >> (index * 2)) & 0x3;

		printk("Status for %u was %d\n", j, status);
		if (status == PAGE_VALID) {

			for (i = 0; i < data_config.nb_blocks *
					data_config.pages_per_block; i++) {
				if (mapper[i] == ppage) {
					ret = create_mapping_new_block(i, &npage, block_counter);

					if (ret < 0) {
						printk(PRINT_PREF "Creating mapping for migration failed\n");
						return ret;
					}

					printk("vpage was %llu\n", i);
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

			bitmap[offset] = (bitmap[offset] &
					  ~(0x3 << index * 2)) |
				(PAGE_FREE << index * 2);
			printk("Moving page %llu to page %llu\n", ppage, npage);

		} else if (status == PAGE_INVALID) {
			bitmap[offset] = (bitmap[offset] &
					  ~(0x3 << index * 2)) |
				(PAGE_FREE << index * 2);

			for (i = 0; i < data_config.nb_blocks *
					data_config.pages_per_block; i++) {
				if (mapper[i] == ppage) {
					mapper[i] = PAGE_GARBAGE_RECLAIMED;
					break;
					printk("Reclaiming %llu\n", i);
				}
			}

			total_written_page--;
		}

		index++;

		if (index == 4) {
			offset++;
			index=0;
		}

		j++;
		ppage++;
	}

	return 0;
}

int garbage_collection(int threshold)
{
	uint64_t i = 0;
	uint64_t offset = 0;
	int index = 0;
	uint8_t status;
	uint64_t num_pages = data_config.nb_blocks *
		data_config.pages_per_block;
	uint64_t invalid_page_counter = 0;
	uint64_t page_per_block_counter = 0;
	uint64_t block_counter = 0;
	int ret;

	while (i < num_pages) {

		status = (bitmap[offset] >> (index * 2)) & 0x3;

		if (status == PAGE_INVALID)
			invalid_page_counter++;

		index++;

		if (index == 4) {
			offset++;
			index=0;
		}
		i++;
		page_per_block_counter++;

		if (page_per_block_counter == data_config.pages_per_block) {

			if (invalid_page_counter >= data_config.pages_per_block
								/ threshold) {
				printk("Migration %llu \n", block_counter);
				ret = migrate(block_counter);

				if (ret) {
					printk(PRINT_PREF "Migration of block %llu for garbage collection failed \n", block_counter);
					return -1;
				}

				if (erase_block(block_counter, 1, &data_config)) {
				
					printk(PRINT_PREF "erase block %llu for garbage collection failed \n", block_counter);
					return -1;
				}
			}
			block_counter++;
			page_per_block_counter = 0;
			invalid_page_counter = 0;
		}
	}

	return 0;
}

int get_free_page(uint64_t *ppage)
{
	if (data_config.read_only) {
		printk(PRINT_PREF "No free pages to give \n");
		return -1;
	}

	*ppage = current_free_page;


	fix_free_page_pointer(current_free_page+1);

	return 0;
}


int create_mapping_new_block(uint64_t vpage, uint64_t *ppage,
			     uint64_t blk_number)
{
	int ret = get_free_page(ppage);
	uint64_t offset = *ppage / 4;
	int index = *ppage % 4;


	while (*ppage >= blk_number * data_config.pages_per_block && *ppage <
	       (blk_number + 1) * data_config.pages_per_block) {

		ret = get_free_page(ppage);
		if (ret != 0) {
			printk(PRINT_PREF "could not create mapping due to no free page\n");
			return ret;
		}

		offset = *ppage / 4;
		index = *ppage % 4;
	}

	mapper[vpage] = *ppage;

	bitmap[offset] = (bitmap[offset] & ~(0x3 << index * 2)) |
			(PAGE_VALID << index * 2);

	total_written_page++;

	printk(PRINT_PREF "%s offset %llu index %d ppage %llx vpage %llx bitmap %x \n", __func__, offset, index, *ppage, vpage, bitmap[offset]);
	return 0;
}

int create_mapping(uint64_t vpage, uint64_t *ppage)
{
	int ret = get_free_page(ppage);
	uint64_t offset = *ppage / 4;
	int index = *ppage % 4;

	if (ret != 0) {
		printk(PRINT_PREF "could not create mapping\n");
		return ret;
	}

	mapper[vpage] = *ppage;

	bitmap[offset] = (bitmap[offset] & ~(0x3 << index * 2)) |
			(PAGE_VALID << index * 2);

	total_written_page++;

	printk(PRINT_PREF "%s offset %llu index %d ppage %llx vpage %llx bitmap %x \n", __func__, offset, index, *ppage, vpage, bitmap[offset]);
	return 0;
}


int create_mapping_multipage(uint64_t vpage, uint32_t num_pages)
{
	uint32_t page = 0;
	uint64_t lpage = vpage;
	uint64_t ppage;

	while (page < num_pages) {
		if (mapper[lpage] != PAGE_UNALLOCATED &&
		    mapper[lpage] != PAGE_GARBAGE_RECLAIMED) {
		    	printk("multipage mapping now allowed for %llu \n", lpage);
			return -1;
		}
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


uint8_t get_ppage_state(uint64_t ppage)
{
	uint64_t offset = ppage / 4;
	int index = ppage % 4;

	return (bitmap[offset] >> (index * 2)) & 0x3;
}

int get_existing_mapping(uint64_t vpage, uint64_t *ppage)
{
	uint64_t offset;
	int index;
	uint8_t state;

	*ppage = mapper[vpage];

	printk(PRINT_PREF "%s ppage %llx vpage %llx \n", __func__, *ppage, vpage);

	if (*ppage == PAGE_UNALLOCATED)
		return PAGE_NOT_MAPPED;

	if (*ppage == PAGE_GARBAGE_RECLAIMED)
		return PAGE_RECLAIMED;

	offset = *ppage / 4;
	index = *ppage % 4;

	state = (bitmap[offset] >> (index * 2)) & 0x3;

	printk(PRINT_PREF "%s offset %llu index %d state %d \n",
	       __func__, offset, index, state);

	return state;
}

int mark_vpage_invalid(uint64_t vpage, uint64_t num_pages)
{
	int state;
	uint64_t i = 0;
	uint64_t ppage;
	uint64_t offset;
	int index;

	while (i < num_pages) {

		state = get_existing_mapping(vpage + i, &ppage);

		if (state != PAGE_VALID) {
			printk(PRINT_PREF "Trying to mark a non-valid page as invalid\n");
			return -1;
		}
		i++;

		offset = ppage / 4;
		index = ppage % 4;

		bitmap[offset] = (bitmap[offset] & ~(0x3 << index * 2)) |
			(PAGE_INVALID << index * 2);

		printk(PRINT_PREF "%s offset %llu index %d ppage %llu vpage %llu bitmap %x \n", __func__, offset, index, ppage, vpage, bitmap[offset]);
	}

	return 0;
}


void flush_meta_data_to_flash(lkp_kv_cfg *config)
{
	uint64_t total_pages = mapper_pages + bitmap_pages + 1;
	uint32_t block_count;
	uint8_t *byte_mapper = (uint8_t *)mapper;
	size_t i = 0;
	size_t j = 0;

	if (total_pages % config->pages_per_block)
		block_count = total_pages / config->pages_per_block + 1;
	else
		block_count = total_pages / config->pages_per_block;

	if (erase_block(0, block_count, config)) {
		printk("Erasing the block device failed while flushing\n");
		return;
	}

	create_meta_data(config);

	for (i = bitmap_start; i <= bitmap_pages ; i++) {
		if (write_page(i, bitmap + (j) * config->page_size,
			      config) != 0) {
			printk(PRINT_PREF "Write for %lu page failed\n", i);
		}
		j++;
	}

	for (i = mapper_start; i <= mapper_pages ; i++) {
		if (write_page(i, byte_mapper + (j) * config->page_size,
			      config) != 0) {
			printk(PRINT_PREF "Write for %lu page failed\n", i);
		}
		j++;
	}
}

/**
 * Module exit function
 */
static void __exit lkp_kv_exit(void)
{
	printk(PRINT_PREF "Exiting ... \n");

	flush_meta_data_to_flash(&meta_config);

	device_exit();
	destroy_config(&meta_config);
	destroy_config(&data_config);

	if (page_buffer)
		vfree(page_buffer);

	if (bitmap)
		vfree(bitmap);

	if (mapper)
		vfree(mapper);
}

/**
 * Freeing stuff on exit
 */
static void destroy_config(lkp_kv_cfg *config)
{
	put_mtd_device(config->mtd);
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

int erase_block(uint64_t block_index, int block_count, lkp_kv_cfg *config)
{
	struct erase_info ei;

	/* erasing one or several flash blocks is made through the use of an
	 * erase_info structure passed to the MTD NAND driver */

	ei.mtd = config->mtd;
	ei.len = ((uint64_t) config->block_size) * block_count;
	ei.addr = block_index * config->pages_per_block * config->page_size;
	/* the erase operation is made aysnchronously and a callback function will
	 * be executed when the operation is done */
	ei.callback = data_format_callback;

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

	return 0;
}

/**
 * Format operation: we erase the entire flash partition
 */
static int format_config(lkp_kv_cfg *config,
			 void (*callback)(struct erase_info *e))
{
	struct erase_info ei;

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

	config->read_only = 0;

	printk(PRINT_PREF "Format done\n");

	return 0;
}

static int create_meta_data(lkp_kv_cfg *meta_config)
{
	uint32_t *signature = (uint32_t *)page_buffer;
	size_t i;
	int ret;

	for (i = 0; i < meta_config->page_size; i++)
		page_buffer[i] = 0xFF;

	*signature = 0xdeadbeef;

	*(signature+4) = total_written_page;

	ret = write_page(0, page_buffer, meta_config);

	if (ret != 0) {
		printk(PRINT_PREF "Writing page for signature failed\n");
		return ret;
	}

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

	ret = create_meta_data(&meta_config);

	if (ret != 0) {
		printk(PRINT_PREF "Creating metadata failed\n");
		return ret;
	}

	ret = construct_meta_data(&meta_config, &data_config, false);

	if (ret != 0) {
		printk(PRINT_PREF "Constructing metadata failed\n");
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
