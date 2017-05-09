/**
 * This file contains the prototype core functionalities.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include "cache.h"

/* Enables/Disables the caching */
#define ENABLE_CACHE 1

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_32_PRIME 16777619u
#define FNV_32_BASIS 2166136261u

/* Configure the number of cache pages here */
#define NUM_CACHE_PAGES 1000
#define KV_HASH_BITS 20

/* Head of the double link list of LRU cache */
LIST_HEAD(cache_list);

/* Defines HashTable for the LRU cache */
DEFINE_HASHTABLE(kv_index_table, KV_HASH_BITS);

/* Max elements allowed in the cache */
uint32_t total_elements = 0;


/**
 * @brief Hash function for the string`
 *
 * @param str String to be hashed
 * @param bits Maximum allowed bucket size
 *
 * @return The hash for the string
 */
#if ENABLE_CACHE
static unsigned hash_string (const char *str, unsigned bits)
{
  const unsigned char *s = (const unsigned char *) str;
  unsigned hash;

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ *s++;

  return hash % bits;
}
#endif

/**
 * @brief Adds the given key into the hash table
 *
 * @param key Key to be added into the hash table
 * @param ptr Pointer of the cached node in the list
 *
 * @return 0 on success, -ENOMEM on failure
 */
#if ENABLE_CACHE
static int index_insert(const char* key, struct cached_node *ptr)
{
	struct index_item *node;
	node = vmalloc(sizeof(struct index_item));
	if(!node) {
		printk(KERN_INFO "Could not put key in index");
		return -ENOMEM;
	}
	node->key = key;
	node->ptr = ptr;

	hlist_add_head(&node->hlist_elem, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))]);
	return 0;
}
#endif


/**
 * @brief Deletes the given key from the hash table
 *
 * @param key Key to be deleted from the hash table
 */
#if ENABLE_CACHE
static void index_delete(const char *key)
{
	struct index_item *node;
	struct hlist_node *tmp;
	hlist_for_each_entry_safe(node, tmp, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		hlist_del(&node->hlist_elem);
		vfree(node);
	}
}
#endif

/**
 * @brief Deletes the entire hash table
 */
#if ENABLE_CACHE
static void index_clear(void)
{
	int bkt;
	struct index_item *node;
	struct hlist_node *tmp;
	hash_for_each_safe(kv_index_table, bkt, tmp, node, hlist_elem)
	{
		hash_del(&node->hlist_elem);
		vfree(node);
	}
}
#endif

/**
 * @brief Gets the key from the hash table
 *
 * @param key Pointer to the key
 *
 * @return The pointer to the node in dll, otherwise NULL
 */
#if ENABLE_CACHE
static void * index_get(const char* key)
{
	struct index_item *node;
	hlist_for_each_entry(node,
			     &kv_index_table[hash_string(
				key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		if(strcmp(node->key, key) == 0)
			return node->ptr;
	}
	return NULL;
}
#endif

/**
 * @brief Evicts the LRU node from the cache
 */
#if ENABLE_CACHE
static void cache_evict (void)
{
	struct cached_node *node =
		list_first_entry(&cache_list, struct cached_node, list);

	list_del(&node->list);

	index_delete(node->key);

	vfree(node->key);
	vfree(node->val);
	vfree(node);

	total_elements--;
}
#endif

/**
 * @brief Adds the given key, val into the LRU Cache
 *
 * @param key Key to be added
 * @param val Value to be added
 * @param vpage Vpage corresponding to the key
 * @param num_pages Num_pages for the key,val
 */
void project6_cache_add (const char *key,
		const char *val, uint64_t vpage, uint32_t num_pages)
{

#if ENABLE_CACHE
	struct cached_node *node = vmalloc(sizeof(struct cached_node));

	if (!node) {
		printk("Node allocation failed for caching \n");
		return;
	}

	node->key = vmalloc(strlen(key) + 1);
	if (!node->key) {
		printk("Key allocation failed for caching \n");
		return;
	}
	node->val = vmalloc(strlen(val) + 1);
	if (!node->val) {
		printk("Val allocation failed for caching \n");
		return;
	}

	strncpy(node->key, key, strlen(key) + 1);
	strncpy(node->val, val, strlen(val) + 1);

	node->vpage = vpage;
	node->num_pages = num_pages;

	if (total_elements == NUM_CACHE_PAGES) {
		cache_evict();
	}

	list_add_tail(&node->list, &cache_list);

	index_insert(node->key, node);

	total_elements++;
#endif
}

/**
 * @brief Removes a key from the cache
 *
 * @param key The key to be removed
 */
void project6_cache_remove(const char *key)
{
#if ENABLE_CACHE
	struct cached_node *node = index_get(key);

	if (!node) {
		return;
	}

	list_del(&node->list);

	index_delete(node->key);

	vfree(node->key);
	vfree(node->val);
	vfree(node);

	total_elements--;
#endif
}

/**
 * @brief Update the existing entry in cache/add new entry
 *
 * @param key Key to be updated
 * @param val Value of the key which is updated
 * @param vpage vpage for the key
 * @param num_pages Num_pages corresponding to the vpage
 */
void project6_cache_update (const char *key,
		   const char *val, uint64_t vpage, uint32_t num_pages)
{
#if ENABLE_CACHE
	struct cached_node *node = index_get(key);
	void *tmp;

	if (!node) {
		project6_cache_add(key, val, vpage, num_pages);
		return;
	}

	tmp = node->val;

	node->val = vmalloc(strlen(val) + 1);
	if (!node->val) {
		printk("Val allocation failed for caching \n");
		project6_cache_remove(key);
		return;
	}

	vfree(tmp);

	strncpy(node->val, val, strlen(val) + 1);

	node->vpage = vpage;
	node->num_pages = num_pages;

	list_del(&node->list);

	list_add_tail(&node->list, &cache_list);
#endif
}


/**
 * @brief Peforms cache lookup
 *
 * @param key Key to be search
 * @param val Value corresponding to the key
 * @param vpage Vpage corresponding to the key
 * @param num_pages num_pages corresponding to the key
 *
 * @return 0 on failure, 1 on success
 */
int project6_cache_lookup(const char *key, char *val,
		 uint64_t *vpage, uint32_t *num_pages)
{
#if ENABLE_CACHE
	struct cached_node *node = index_get(key);

	if (!node) {
		return 0;
	}

	if (val)
		strcpy(val, node->val);

	*vpage = node->vpage;
	*num_pages = node->num_pages;

	return 1;
#else
	return 0;
#endif
}


/**
 * @brief Deletes the entire cache
 */
void project6_cache_clean(void)
{
#if ENABLE_CACHE
	struct cached_node *node;
	struct cached_node *next;

	if (!list_empty(&cache_list)) {
		list_for_each_entry_safe(node, next, &cache_list, list)
		{
			list_del(&node->list);

			vfree(node->key);
			vfree(node->val);
			vfree(node);

			total_elements--;
		}
	}

	index_clear();
#endif
}
