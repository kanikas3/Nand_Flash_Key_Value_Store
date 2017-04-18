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

#define NEW_KEY 0x20000000
#define PREVIOUS_KEY 0x10000000
#define EMPTY_PAGE 0xFFFFFFFF

#define FREE_PAGE 0x1

/* Taken from http://www.cse.yorku.ca/~oz/hash.html */

uint64_t hash(const char *str)
{
	uint64_t hash = 5381;
	int c;

	while ((c = *str++) && c)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

uint64_t get_mapping (uint64_t vpage)
{
	return vpage;
}

int get_page_status(uint64_t ppage)
{
	return FREE_PAGE;
}

int get_key_page(const char *key, uint64_t ppage, uint64_t vpage,
		 uint64_t *ret_page)
{
	size_t counter = 0;
	char *buffer;
	int key_len;
	int val_len;
	int marker;

	buffer = (char *)vmalloc(data_config.page_size * sizeof(char));

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {
		if (read_page(ppage, buffer, &data_config) != 0) {

			marker = *buffer;

			if (marker & NEW_KEY) {
				key_len = *(buffer + 4);
				val_len = *(buffer + 8);

				if (!strncmp(key, (buffer + 12), key_len)) {
					*ret_page = vpage;
					return 0;
				}
			}

			if (marker & EMPTY_PAGE && get_page_status(ppage) ==
							FREE_PAGE)
				return -1;
		}
		ppage = get_mapping(++vpage);
		counter++;
	}
	return -1;
}

int mark_page_invalid(uint64_t page, uint64_t number)
{
	if (page < 0 ||
	    page > data_config.pages_per_block * data_config.nb_blocks)
		return -1;
	return 0;
}

int get_keyval(const char *key, char *val)
{
	uint64_t vpage;
	uint64_t ppage;
	size_t counter = 0;
	char *buffer;
	int marker;
	int key_len;
	int val_len;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	ppage = get_mapping(vpage);

	buffer = (char *)vmalloc(data_config.page_size * sizeof(char));;

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {
		if (read_page(ppage, buffer, &data_config) != 0) {

			marker = *buffer;

			if (marker & NEW_KEY) {
				key_len = *(buffer + 4);
				val_len = *(buffer + 8);

				if (!strncmp(key, (buffer + 12), key_len)) {
					memcpy(val,
					       buffer + 12 + key_len, val_len);
					val[val_len] = '\0';
					vfree(buffer);
					return 0;
				}
			}

			if (marker & EMPTY_PAGE && get_page_status(ppage) ==
							FREE_PAGE)
				return -1;
		}
		ppage = get_mapping(++vpage);
		counter++;
	}

	vfree(buffer);

	return -1;
}

int set_keyval(const char *key, const char *val)
{
	uint64_t vpage;
	uint64_t ppage;
	uint64_t lpage;
	size_t counter = 0;
	char *buffer;
	int ret = 0;
	int key_len;
	int val_len;
	int marker;
	int i;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	ppage = get_mapping(vpage);

	ret = get_key_page(key, ppage, vpage, &lpage);

	if (ret != -1) {
		if (mark_page_invalid(lpage, 1)) {
			printk(PRINT_PREF "Tried to mark %llu \n", lpage);
			return -1;
		}
	}

	buffer = (char *)vmalloc(data_config.page_size * sizeof(char));

	key_len = strlen(key);

	val_len = strlen(val);

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		if (read_bytes (ppage, (char *)&marker, &data_config, 4) != 0) {

			if (marker & EMPTY_PAGE) {

				/* prepare the buffer we are going to write on flash */
				for (i = 0; i < data_config.page_size; i++)
					buffer[i] = 0x0;

				marker = NEW_KEY;

				memcpy(buffer, &marker, 4);

				/* key size ... */
				memcpy(buffer + 4, &key_len, sizeof(int));

				/* ... value size ... */
				memcpy(buffer + sizeof(int) + 4, &val_len, sizeof(int));

				/* ... the key itself ... */
				memcpy(buffer + 2 * sizeof(int) + 4,
					key, key_len);

				/* ... then the value itself. */
				memcpy(buffer + 2 * sizeof(int) + key_len + 4,
					val, val_len);

				ret = write_page(ppage, buffer, &data_config);

				vfree(buffer);

				if (ret) {
					printk(PRINT_PREF "Writing page %llu failed", ppage);
				}
				return ret;
			}
		} else {
			if (ret) {
				printk(PRINT_PREF "Reading page %llu failed",
				       ppage);
			}
			return ret;
		}

		ppage = get_mapping(++vpage);
		counter++;
	}

	vfree(buffer);
	return -1;
}

int delete_key(const char *key)
{
	int ret;
	uint64_t ppage;
	uint64_t vpage;
	uint64_t lpage;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	ppage = get_mapping(vpage);

	ret = get_key_page(key, ppage, vpage, &lpage);

	if (ret != -1) {
		if (mark_page_invalid(lpage, 1)) {
			printk(PRINT_PREF "Tried to mark %llu \n", lpage);
			return -1;
		}
	}

	printk(PRINT_PREF "Could not find key %s\n", key);

	return -1;
}

