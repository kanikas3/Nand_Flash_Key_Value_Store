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

#define PRINT_PREF KERN_INFO "[KEY_VAL]: "

#define NEW_KEY 0x20000000
#define PREVIOUS_KEY 0x10000000

/* Taken from http://www.cse.yorku.ca/~oz/hash.html */
static uint64_t hash(const char *str)
{
	uint64_t hash = 5381;
	int c;

	while ((c = *str++) && c)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash % (data_config.nb_blocks * data_config.pages_per_block);
}

/**
 * @brief Updates the data on the flash
 *
 * @param len Length of the data to be updated
 * @param buffer Buffer to be copied
 * @param vpage Virtual page from where write starts
 *
 * @return 0 for success, appropriate error codes on failure
 */
static int update_data_flash(uint32_t len, const char *buffer, uint64_t *vpage)
{
	uint32_t size;
	uint32_t count = 0;
	int ret;
	uint32_t marker;
	uint64_t ppage;
	uint8_t state;

	while (len) {
		if (len > data_config.page_size - 4) {
			size = data_config.page_size - 4;
		} else
			size = len;

		memset(page_buffer, 0x0, data_config.page_size);

		marker = PREVIOUS_KEY;

		memcpy(page_buffer, &marker, sizeof(uint32_t));

		memcpy(page_buffer + 4, buffer + count, size);

		state = project6_get_existing_mapping(++(*vpage), &ppage);

		if (state == PAGE_NOT_MAPPED) {
			printk(PRINT_PREF "Overflow happened for vpage in updating flash\n");
			return -EPERM;
		}

		ret = write_page(ppage, page_buffer, &data_config);

		if (ret) {
			printk(PRINT_PREF "Writing page 0x%llx failed", ppage);
			return ret;
		}

		count += size;
		len -= size;
	}

	return 0;
}

/**
 * @brief Finds the value for the given key
 *
 * @param val Pointer of the value to be found
 * @param key_len Length of key which needs to be skipped
 * @param val_len Length of the value to be found
 * @param num_pages Number of pages to be searched
 * @param vpage Virtual page to start search
 *
 * @return True if found, False if not found
 */
bool find_value(char *val, uint32_t key_len,
	       uint32_t val_len, uint32_t num_pages, uint64_t vpage)
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
		return true;
	} else {
		if (key_len >= data_config.page_size - 16) {
			key_len = key_len - (data_config.page_size - 16);
			lpage++;
			lpage = lpage + (key_len) / (data_config.page_size - 4);
			if (key_len % (data_config.page_size - 4))
				offset = key_len %
					(data_config.page_size - 4) + 4;
			else
				offset = 4;
		} else {

			offset = key_len + 16;
		}

		while (lpage < vpage + num_pages && val_len != 0) {

			if (data_config.page_size - offset >= val_len)
				size = val_len;
			else
				size = data_config.page_size - offset;

			state = project6_get_existing_mapping(lpage, &ppage);

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

/**
 * @brief Helper function to compare the key with contents on flash
 *
 * @param key Key to be searched
 * @param num_pages Number of pages for the key/value
 * @param key_len Length of the key
 * @param vpage Vpage to be searched
 *
 * @return  True if found, False if not found
 */
bool find_key(const char *key, uint32_t num_pages,
	      uint32_t key_len, uint64_t vpage)
{
	uint64_t pages = 1;
	uint32_t size;
	uint8_t state;
	uint64_t ppage;
	uint32_t count;
	int ret;

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

				state = project6_get_existing_mapping(++vpage,
								      &ppage);

				if (state == PAGE_VALID) {

					ret = read_page(ppage, page_buffer,
						      &data_config);
					if (!ret) {

						if (strncmp(key + count,
							    (page_buffer + 4),
							    size))
							return false;
					} else {
						printk(PRINT_PREF "Find key read cmd failed 0x%llx\n", ppage);
						return false;
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

/**
 * @brief Finds the virtual page for the given key on flash
 *
 * @param key Key to be searched
 * @param vpage Starting vpage given by hash
 * @param ret_page Pointer to the Vpage to be returned
 * @param num_pages Number of pages to be returned
 *
 * @return 0 for success, appropriate failure codes
 */
int get_key_page(const char *key, uint64_t vpage,
		 uint64_t *ret_page, uint32_t *num_pages)
{
	size_t counter = 0;
	uint32_t key_len;
	uint32_t marker;
	uint64_t ppage;
	uint8_t state;

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = project6_get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED)
			return -EINVAL;

		if (state == PAGE_VALID) {

			if (read_page(ppage, page_buffer, &data_config) == 0) {

				marker = *((uint32_t *)page_buffer);

				if (marker & NEW_KEY) {
					*num_pages =
						*((uint32_t *)(page_buffer+4));
					key_len = strlen(key);

					if (key_len !=
					    *((uint32_t *)(page_buffer + 8)))
						break;

					if (find_key(key, *num_pages,
						     key_len, vpage)) {
						*ret_page = vpage;
						return 0;
					}
				}

			}

		}

		vpage = (vpage + 1) % (data_config.nb_blocks *
				       data_config.pages_per_block);
		counter++;
	}
	return -EINVAL;
}

/**
 * @brief Gets the value for given key
 *
 * @param key Key to be searched
 * @param val Pointer for the value
 *
 * @return 0 for success, -1 for failure
 */
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
	int ret;

	if (project6_cache_lookup(key, val, &vpage, &num_pages)) {
		printk("GET_KEY: %s key, %s val\n",
		       key, val);
		return 0;
	}

	vpage = hash(key);

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = project6_get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED) {
			printk("Get key failed as key was not found\n");
			return -1;
		}

		if (state == PAGE_VALID) {
			ret = read_page(ppage, page_buffer, &data_config);
			if (ret) {
				printk("Reading page has failed in get key\n");
				return -1;
			}

			marker = *((uint32_t *)page_buffer);

			if (marker & NEW_KEY) {
				key_len = strlen(key);
				val_len = *((uint32_t *)(page_buffer + 12));
				num_pages = *((uint32_t *)(page_buffer + 4));


				if (find_key(key, num_pages, key_len,
					     vpage)) {
					if (!find_value(val, key_len,
						val_len, num_pages, vpage)) {
						printk("Get key failed as key was not found on flash\n");
						return -1;
					}
					printk("GET_KEY: %d key_len %d val_len %s key, %s val\n",
					       key_len, val_len, key, val);
					project6_cache_add(key, val, vpage,
							   num_pages);
					return 0;
				}
			}
		}
		vpage = (vpage + 1) % (data_config.nb_blocks *
				       data_config.pages_per_block);
		counter++;
	}

	printk("Get key failed as key was not found\n");
	return -1;
}

/**
 * @brief Performs set/update of key
 *
 * @param key Key to be updated/set
 * @param val Value for the given key
 *
 * @return 0 for success, -1 for failure
 */
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
	    (data_config.nb_blocks * data_config.pages_per_block) / 2) {

		if (project6_garbage_collection(2)) {
			printk(PRINT_PREF "garbage collection has failed\n");
		}
	}

	if (!project6_cache_lookup(key, NULL, &vpage, &num_pages)) {

		vpage = hash(key);

		ret = get_key_page(key, vpage, &lpage, &num_pages);

		if (!ret) {
			ret = project6_mark_vpage_invalid(lpage, num_pages);

			if (ret) {
				printk(PRINT_PREF "Mark invalid failed for 0x%llx num %d \n",
				       lpage, num_pages);
			}
		}
	} else {
		ret = project6_mark_vpage_invalid(vpage, num_pages);

		if (ret) {
			printk(PRINT_PREF "Mark invalid failed for 0x%llx num %d\n",
			       vpage, num_pages);
		}
	}

	key_len = strlen(key);

	val_len = strlen(val);

	printk("SET_KEY: %d key_len %d val_len %s key, %s val\n",
	       key_len, val_len, key, val);

	if ((12 + key_len + val_len) % (data_config.page_size - 4) == 0)
		num_pages = (12 + key_len + val_len) /
			(data_config.page_size - 4);
	else
		num_pages = (12 + key_len + val_len) /
			(data_config.page_size - 4) + 1;

	printk("Number of page is %d val_len %d key %d vpage %llx\n",
	       num_pages, val_len, key_len, vpage);

	while (counter <= data_config.nb_blocks * data_config.pages_per_block) {

		state = project6_get_existing_mapping(vpage, &ppage);

		if (state == PAGE_NOT_MAPPED || state == PAGE_RECLAIMED) {

			ret = project6_create_mapping_multipage(vpage,
								num_pages);
			if (ret == 0) {

				project6_cache_update(key, val,
						      vpage, num_pages);

				project6_get_existing_mapping(vpage, &ppage);

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

						project6_get_existing_mapping(++vpage, &ppage);

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

		vpage = (vpage + 1) % (data_config.nb_blocks *
				       data_config.pages_per_block);
		counter++;
	}

	printk("Set key failed as no space was found\n");
fail:
	project6_cache_remove(key);
	return -1;
}

/**
 * @brief Deletes the given key
 *
 * @param key String for the key
 *
 * @return 0 on success, -1 for failure
 */
int del_keyval(const char *key)
{
	int ret = 0;
	uint64_t vpage;
	uint64_t lpage;
	uint32_t num_pages = 0;

	if (total_written_page >
	    (data_config.nb_blocks * data_config.pages_per_block) / 2) {

		if (project6_garbage_collection(2)) {
			printk(PRINT_PREF "garbage collection has failed\n");
		}
	}

	if (!project6_cache_lookup(key, NULL, &vpage, &num_pages)) {
		vpage = hash(key);

		ret = get_key_page(key, vpage, &lpage, &num_pages);

		if (!ret) {
			ret = project6_mark_vpage_invalid(lpage, num_pages);

			if (ret) {
				printk(PRINT_PREF "Mark invalid failed for 0x%llx num %d \n",
				       lpage, num_pages);
			}
		}
	} else {
		ret = project6_mark_vpage_invalid(vpage, num_pages);
		if (ret) {
			printk(PRINT_PREF "Mark invalid failed for 0x%llx num %d\n",
			       vpage, num_pages);
		}
		project6_cache_remove(key);
	}
	if (ret) {
		printk(PRINT_PREF "Could not delete key %s\n", key);
		return -1;
	}
	else
		printk(PRINT_PREF "Deleted key %s\n", key);

	return 0;
}

