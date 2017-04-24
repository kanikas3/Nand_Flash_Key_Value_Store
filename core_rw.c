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

/* Taken from http://www.cse.yorku.ca/~oz/hash.html */

uint64_t hash(const char *str)
{
	uint64_t hash = 5381;
	int c;

	while ((c = *str++) && c)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

int get_key_page(const char *key, uint64_t vpage,
		 uint64_t *ret_page)
{
	size_t counter = 0;
	int key_len;
	int val_len;
	int marker;
	uint64_t ppage;
	uint8_t state;

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED)
			return -1;

		if (state == PAGE_VALID) {

			if (read_page(ppage, page_buffer, &data_config) == 0) {

				marker = *((int *)page_buffer);

				if (marker & NEW_KEY) {
					key_len = *((int *)(page_buffer + 4));
					val_len = *((int *)(page_buffer + 8));

					if (!strncmp(key, (page_buffer + 12), key_len)) {
						*ret_page = vpage;
						printk("get key page %llx\n", vpage);
						return 0;
					}
				}

			}

		}

		vpage++;
		printk("get key page try %llx\n", vpage);
		counter++;
	}
	return -1;
}

int get_keyval(const char *key, char *val)
{
	uint64_t vpage;
	uint64_t ppage;
	size_t counter = 0;
	int marker;
	int key_len;
	int val_len;
	uint8_t state;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	printk(PRINT_PREF "%s vpage %llx key %s\n", __func__, vpage, key);;

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED) {
			printk("Get key failed as key was not found\n");
			return -1;
		}

		if (state == PAGE_VALID) {
			if (read_page(ppage, page_buffer, &data_config) != 0) {
				printk("Reading page has failed in get key\n");
				return -1;
			}

			marker = *((int *)page_buffer);

			printk("marker is %x\n", marker);

			if (marker & NEW_KEY) {
				key_len = *((int *)(page_buffer + 4));
				val_len = *((int *)(page_buffer + 8));

				printk("key_len is %d\n", key_len);
				printk("val_len is %d\n", val_len);

				if (!strncmp(key, (page_buffer + 12), key_len)) {
					memcpy(val,
					       page_buffer + 12 + key_len, val_len);
					val[val_len] = '\0';
					return 0;
				}
				printk("strcmp has failed\n");
			}
		}
		vpage++;
		counter++;
	}

	printk("Get key failed as key was not found\n");
	return -1;
}

int set_keyval(const char *key, const char *val)
{
	uint64_t vpage;
	uint64_t ppage;
	uint64_t lpage;
	size_t counter = 0;
	int ret = 0;
	int key_len;
	int val_len;
	int marker;
	uint8_t state;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	printk(PRINT_PREF "%s vpage %llx key %s val %s\n", __func__, vpage, key, val);

	ret = get_key_page(key, vpage, &lpage);

	if (ret != -1) {
		if (mark_vpage_invalid(lpage, 1)) {
			printk(PRINT_PREF "Tried to mark %llu vpage\n", lpage);
			return -1;
		}
	}

	key_len = strlen(key);

	val_len = strlen(val);

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED || state == PAGE_RECLAIMED) {
			if (create_mapping(vpage, &ppage)) {
				printk(PRINT_PREF "Create mapping failed\n");
				return -1;
			}

			/* prepare the buffer we are going to write on flash */
			memset(page_buffer, 0x0, data_config.page_size);

			marker = NEW_KEY;

			memcpy(page_buffer, &marker, 4);

			/* key size ... */
			memcpy(page_buffer + 4, &key_len, sizeof(int));

			/* ... value size ... */
			memcpy(page_buffer + sizeof(int) + 4, &val_len, sizeof(int));

			/* ... the key itself ... */
			memcpy(page_buffer + 2 * sizeof(int) + 4,
			       key, key_len);

			/* ... then the value itself. */
			memcpy(page_buffer + 2 * sizeof(int) + key_len + 4,
			       val, val_len);

			ret = write_page(ppage, page_buffer, &data_config);

			if (ret) {
				printk(PRINT_PREF "Writing page %llu failed", ppage);
			}

			printk(PRINT_PREF "Page %llx written successfully\n", ppage);
			return ret;
		}

		++vpage;
		counter++;
	}

	printk("Set key failed as no space was found\n");
	return -1;
}

int del_keyval(const char *key)
{
	int ret;
	uint64_t vpage;
	uint64_t lpage;

	vpage = hash(key) % data_config.nb_blocks * data_config.pages_per_block;

	ret = get_key_page(key, vpage, &lpage);

	if (ret != -1) {
		if (mark_vpage_invalid(lpage, 1)) {
			printk(PRINT_PREF "Tried to mark %llu \n", lpage);
			return -1;
		}

		if (garbage_collection(32)) {
			printk("Garbage collection failed\n");
		}

			if (read_page(5, page_buffer, &data_config) == 0) {
				lpage = *((uint64_t *) page_buffer);
				printk("Lpage was %llx\n", lpage);
			}
			if (read_page(2, page_buffer, &data_config) == 0) {
				lpage = *((uint64_t *) page_buffer);
				printk("Lpage was %llx\n", lpage);
			}
		return 0;
	}

	printk(PRINT_PREF "Could not find key %s\n", key);

	return -1;
}

