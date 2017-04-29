/**
 * This file contains the prototype core functionalities.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
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

int update_data_flash(uint32_t len, const char *buffer, uint64_t *vpage)
{
	uint32_t size;
	uint32_t count = 0;
	int ret;
	uint32_t marker;
	uint64_t ppage;

	while (len) {
		if (len > data_config.page_size - 4) {
			size = data_config.page_size - 4;
		} else
			size = len;

		memset(page_buffer, 0x0, data_config.page_size);

		marker = PREVIOUS_KEY;

		memcpy(page_buffer, &marker, sizeof(uint32_t));

		memcpy(page_buffer + 4, buffer + count, size);

		get_existing_mapping(++(*vpage), &ppage);

		printk("%s vpage %llx ppage %llx size %d len %d\n",__func__, *vpage, ppage, size, len);
		ret = write_page(ppage, page_buffer, &data_config);

		if (ret) {
			printk(PRINT_PREF "Writing page %llu failed", ppage);
			return ret;
		}

		count += size;
		len -= size;
	}

	return 0;
}

bool get_value(char *val, uint32_t key_len, uint32_t val_len, uint32_t num_pages, uint64_t vpage)
{
	uint64_t lpage = vpage;
	uint64_t ppage;
	uint32_t size;
	uint32_t copied = 0;
	uint32_t offset;
	uint8_t state;

	if (num_pages == 1) {
		memcpy(val,
		       page_buffer + 16 + key_len, val_len);
		val[val_len] = '\0';
		printk("values was %s\n", val);
		return true;
	} else {
		if (key_len >= data_config.page_size - 16) {
			key_len = key_len - (data_config.page_size - 16);
			lpage++;
			lpage = lpage + (key_len) / (data_config.page_size - 4);
			if (key_len % (data_config.page_size - 4))
				offset = key_len % (data_config.page_size - 4) + 4;
			else
				offset = 4;
		} else {

			offset = key_len + 16;
		}

		printk("get_value has lpage %llx vpage %llx offset %d \n", lpage, vpage, offset);
		while (lpage < vpage + num_pages && val_len != 0) {

			if (data_config.page_size - offset >= val_len)
				size = val_len;
			else
				size = data_config.page_size - offset;

			state = get_existing_mapping(lpage, &ppage);

			printk("get key page %llx ppgage %llx size %d\n",
			       lpage, ppage, size);

			if (state == PAGE_VALID) {

				if (read_page(ppage, page_buffer,
					      &data_config) == 0) {
					memcpy(val + copied,
					       page_buffer + offset, size);
				} else {
					printk(PRINT_PREF "Reading the page failed in finding key\n");
				}
			} else
				return false;

			offset = 4;
			val_len -= size;
			lpage++;
			copied += size;
		}

		val[copied] = '\0';
		return true;
	}

	return false;
}

bool find_key(const char *key, uint32_t num_pages, uint32_t key_len, uint32_t val_len, uint64_t vpage)
{
	uint64_t pages = 1;
	uint32_t size;
	uint8_t state;
	uint64_t ppage;
	uint32_t count;

	printk("%s numpage %d key_len %d val_len %d vpage %llx\n", __func__, num_pages, key_len, val_len, vpage);
	if (key_len <= data_config.page_size - 16) {

		if (!strncmp(key, (page_buffer + 16), key_len)) {
			return true;
		}
	} else {

		if (!strncmp(key, (page_buffer + 16),
			     data_config.page_size - 16)) {

			key_len = key_len - (data_config.page_size - 16);

			count = data_config.page_size - 16;

			while (pages < num_pages && key_len != 0) {


				if (data_config.page_size - 4 >= key_len)
					size = key_len;
				else
					size = data_config.page_size - 4;

				state = get_existing_mapping(++vpage, &ppage);

				printk("get key page %llx ppgage %llx size %d\n",
				       vpage, ppage, size);

				if (state == PAGE_VALID) {

					if (read_page(ppage, page_buffer,
						      &data_config) == 0) {

						if (strncmp(key + count, (page_buffer + 4),
							    size))
							return false;
					} else {
						printk(PRINT_PREF "Reading the page failed in finding key\n");
					}
				} else
					return false;
				key_len -= size;
				pages++;
			}
			if (key_len == 0)
				return true;
		}

		return false;
	}

	return false;
}

int get_key_page(const char *key, uint64_t vpage,
		 uint64_t *ret_page, uint32_t *num_pages)
{
	size_t counter = 0;
	uint32_t key_len;
	uint32_t val_len;
	uint32_t marker;
	uint64_t ppage;
	uint8_t state;

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED)
			return -1;

		if (state == PAGE_VALID) {

			if (read_page(ppage, page_buffer, &data_config) == 0) {

				marker = *((uint32_t *)page_buffer);

				if (marker & NEW_KEY) {
					*num_pages = *((uint32_t *)(page_buffer + 4));
					key_len = strlen(key);
					val_len = *((uint32_t *)(page_buffer + 12));

					if (find_key(key, *num_pages, key_len, val_len, vpage)) {
						*ret_page = vpage;
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
	uint32_t marker;
	uint32_t key_len;
	uint32_t val_len;
	uint32_t num_pages;
	uint8_t state;

	if (cache_lookup(key, val, &vpage, &num_pages)) {
		return 0;
	}

	vpage = hash(key) % (data_config.nb_blocks * data_config.pages_per_block);

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

			marker = *((uint32_t *)page_buffer);

			printk("marker is %x\n", marker);

			if (marker & NEW_KEY) {
				key_len = strlen(key);
				val_len = *((uint32_t *)(page_buffer + 12));
				num_pages = *((uint32_t *)(page_buffer + 4));


				if (find_key(key, num_pages, key_len, val_len, vpage)) {
					if (get_value(val, key_len, val_len, num_pages, vpage) == false) {
						printk("getting the value failed \n");
						return -1;
					}
					printk("GET_KEY: %d key_len %d val_len %s key, %s val\n", key_len, val_len, key, val);
					cache_add(key, val, vpage, num_pages);
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
	uint32_t num_pages = 0;
	uint32_t val_count = 0;
	uint32_t key_count = 0;
	uint32_t key_max_write = 0;
	uint32_t size;

	if (total_written_page >
	    1 * (data_config.nb_blocks * data_config.pages_per_block) / 4) {

		if (garbage_collection(2)) {
			printk(PRINT_PREF "garbage collection has failed\n");
		}
	}

	if (!cache_lookup(key, NULL, &vpage, &num_pages)) {

		printk("Lookup failed\n");

		vpage = hash(key) % (data_config.nb_blocks * data_config.pages_per_block);

		printk(PRINT_PREF "%s vpage %llx key %s val %s\n", __func__, vpage, key, val);

		ret = get_key_page(key, vpage, &lpage, &num_pages);

		if (ret != -1) {
			if (mark_vpage_invalid(lpage, num_pages)) {
				printk(PRINT_PREF "Tried to mark %llu vpage\n", lpage);
				return -1;
			}
		}
	} else {
		printk("Lookup passed\n");
		if (mark_vpage_invalid(vpage, num_pages)) {
			printk(PRINT_PREF "Tried to mark %llu vpage\n", lpage);
			return -1;
		}
	}

	key_len = strlen(key);

	val_len = strlen(val);

	printk("SET_KEY: %d key_len %d val_len %s key, %s val\n", key_len, val_len, key, val);
	if ((12 + key_len + val_len) % (data_config.page_size - 4) == 0)
		num_pages = (12 + key_len + val_len) /
			(data_config.page_size - 4);
	else
		num_pages = (12 + key_len + val_len) /
			(data_config.page_size - 4) + 1;

	printk("Number of page is %d val_len %d key %d vpage %llx\n", num_pages, val_len, key_len, vpage);

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED || state == PAGE_RECLAIMED) {

			ret = create_mapping_multipage(vpage, num_pages);
			if (ret == 0) {

				cache_update(key, val, vpage, num_pages);

				get_existing_mapping(vpage, &ppage);

				/* prepare the buffer we are going to write on flash */
				memset(page_buffer, 0x0, data_config.page_size);

				marker = NEW_KEY;

				memcpy(page_buffer, &marker, sizeof(uint32_t));

				memcpy(page_buffer + 4, &num_pages, sizeof(uint32_t));

				/* key size ... */
				memcpy(page_buffer + 8, &key_len, sizeof(uint32_t));

				/* ... value size ... */
				memcpy(page_buffer + 12, &val_len, sizeof(uint32_t));

				if (key_len + val_len + 16 <= data_config.page_size) {
					memcpy(page_buffer + 16,
					       key, key_len);

					memcpy(page_buffer + 16 + key_len,
					       val, val_len);


					ret = write_page(ppage, page_buffer, &data_config);

					if (ret) {
						printk(PRINT_PREF "Writing page %llu failed", ppage);
						goto fail;
					}
					printk(PRINT_PREF "Page %llx written successfully\n", ppage);
					return ret;
				} else if (key_len + 16 <= data_config.page_size) {
					memcpy(page_buffer + 16,
					       key, key_len);

					memcpy(page_buffer + 16 + key_len,
					       val, data_config.page_size - 16 -key_len);

					ret = write_page(ppage, page_buffer, &data_config);

					if (ret) {
						printk(PRINT_PREF "Writing page %llu failed", ppage);
						goto fail;
					}

					val_len = val_len - (data_config.page_size - (16 + key_len));

					val_count = data_config.page_size - (16 + key_len);

					printk("Second config val_len %d count %d\n", val_len, val_count);
					ret = update_data_flash(val_len, val + val_count, &vpage);

					if (ret) {
						printk(PRINT_PREF "Updating the data on flash failed\n");
						goto fail;
					}

					return ret;
				} else {
					memcpy(page_buffer + 16,
					       key, data_config.page_size - 16);

					ret = write_page(ppage, page_buffer, &data_config);

					if (ret) {
						printk(PRINT_PREF "Writing page %llu failed", ppage);
						goto fail;
					}
					printk(PRINT_PREF "Page %llx written successfully\n", ppage);

					key_len = key_len - (data_config.page_size - 16);

					key_count = data_config.page_size - 16;

					key_max_write = key_len - key_len % (data_config.page_size - 4);

					printk("Third config key_len %d key_count %d key_max_write %d\n", key_len, key_count, key_max_write);
					ret = update_data_flash(key_max_write, key + key_count, &vpage);

					if (ret) {
						printk(PRINT_PREF "Updating the key data on flash failed\n");
						goto fail;
					}

					key_count += key_max_write;
					key_len = key_len - key_max_write;

					if (key_len) {
						memset(page_buffer, 0x0, data_config.page_size);

						marker = PREVIOUS_KEY;

						memcpy(page_buffer, &marker, sizeof(uint32_t));

						memcpy(page_buffer + 4, key + key_count, key_len);

						if (val_len > data_config.page_size - 4 - key_len) {
							size = data_config.page_size - 4 - key_len;
						} else
							size = val_len;

						printk("key len %d size %d key count %d\n", key_len, size, key_count);
						memcpy(page_buffer + 4 + key_len, val, size);

						val_len -= size;
						val_count += size;

						get_existing_mapping(++vpage, &ppage);

						printk("val len %d val count %d page %llx\n", val_len, val_count, ppage);
						ret = write_page(ppage, page_buffer, &data_config);

						if (ret) {
							printk(PRINT_PREF "Writing page %llu failed", ppage);
							goto fail;
						}
					}

					if (val_len) {
						ret = update_data_flash(val_len, val + val_count, &vpage);

						if (ret) {
							printk(PRINT_PREF "Updating the val data on flash failed\n");
							goto fail;
						}
					}

				}

				return ret;
			} else if (ret == -ENOMEM) {
				printk(PRINT_PREF "No memory to perform mapping \n");
				goto fail;
			}
		}

		vpage++;
		counter++;
	}

	printk("Set key failed as no space was found\n");
fail:
	cache_remove(key);
	return -1;
}

int del_keyval(const char *key)
{
	int ret;
	uint64_t vpage;
	uint64_t lpage;
	uint32_t num_pages = 0;

	if (total_written_page >
	    1 * (data_config.nb_blocks * data_config.pages_per_block) / 4) {

		if (garbage_collection(2)) {
			printk(PRINT_PREF "garbage collection has failed\n");
		}
	}

	if (!cache_lookup(key, NULL, &vpage, &num_pages)) {
		vpage = hash(key) % (data_config.nb_blocks * data_config.pages_per_block);

		ret = get_key_page(key, vpage, &lpage, &num_pages);

		if (ret != -1) {
			if (mark_vpage_invalid(lpage, num_pages)) {
				printk(PRINT_PREF "Tried to mark %llu \n", lpage);
				return -1;
			}
			return 0;
		}
	} else {
		if (mark_vpage_invalid(vpage, num_pages)) {
			printk(PRINT_PREF "Tried to mark %llu \n", vpage);
			return -1;
		}
		cache_remove(key);
		return 0;
	}
	printk(PRINT_PREF "Could not find key %s\n", key);

	return -1;
}

