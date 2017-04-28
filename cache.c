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

/* Fowler-Noll-Vo hash constants, for 32-bit word sizes. */
#define FNV_32_PRIME 16777619u
#define FNV_32_BASIS 2166136261u

/* Configure the number of cache pages here */
#define NUM_CACHE_PAGES 64
#define KV_HASH_BITS 6

LIST_HEAD(cache_list);

DEFINE_HASHTABLE(kv_index_table, KV_HASH_BITS);

uint32_t total_elements = 0;

#define ENABLE_CACHE 1

struct cached_node
{
    uint64_t vpage;

    uint32_t num_pages;

    char *key;

    char *val;

    struct list_head list;
};

struct index_item {
     const char* key;

     struct cached_node *ptr;

     struct hlist_node hlist_elem;
};

#if ENABLE_CACHE
static unsigned
hash_string (const char *s_, unsigned bits)
{
  const unsigned char *s = (const unsigned char *) s_;
  unsigned hash;

  hash = FNV_32_BASIS;
  while (*s != '\0')
    hash = (hash * FNV_32_PRIME) ^ *s++;

  return hash % bits;
}
#endif

void
index_put(const char* key, struct cached_node *ptr)
{
#if ENABLE_CACHE
	struct index_item *node;
	node = vmalloc(sizeof(struct index_item));
	if(!node) {
		printk(KERN_INFO "Could not put key in index");
		return;
	}
	node->key = key;
	node->ptr = ptr;

	hlist_add_head(&node->hlist_elem, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))]);
#endif
}


void
index_delete(const char *key)
{
#if ENABLE_CACHE
	struct index_item *node;
	struct hlist_node *tmp;
	hlist_for_each_entry_safe(node, tmp, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		hlist_del(&node->hlist_elem);
		vfree(node);
	}
#endif
}

void index_clear(void)
{
#if ENABLE_CACHE
	int bkt;
	struct index_item *node;
	struct hlist_node *tmp;
	hash_for_each_safe(kv_index_table, bkt, tmp, node, hlist_elem)
	{
		hash_del(&node->hlist_elem);
		vfree(node);
	}
#endif
}

void *
index_get(const char* key)
{
#if ENABLE_CACHE
	struct index_item *node;
	hlist_for_each_entry(node, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		if(strcmp(node->key, key) == 0)
			return node->ptr;
	}
#endif
	return NULL;

}

void cache_evict (void)
{
#if ENABLE_CACHE
	struct cached_node *node = list_first_entry(&cache_list, struct cached_node, list);

	list_del(&node->list);

	index_delete(node->key);

	vfree(node->key);
	vfree(node->val);
	vfree(node);

	total_elements--;
#endif
}

void cache_add (const char *key, const char *val, uint64_t vpage, uint32_t num_pages)
{

#if ENABLE_CACHE
	struct cached_node *node = vmalloc(sizeof(struct cached_node));

	if (!node) {
		printk("Node allocation failed for caching \n");
		return;
	}

	node->key = vmalloc(strlen(key) + 1);
	node->val = vmalloc(strlen(val) + 1);

	strncpy(node->key, key, strlen(key) + 1);
	strncpy(node->val, val, strlen(val) + 1);

	node->vpage = vpage;
	node->num_pages = num_pages;

//	printk("CACHE add %s %s \n", key, val);
	if (total_elements == NUM_CACHE_PAGES) {
		cache_evict();
	}

	list_add_tail(&node->list, &cache_list);

	index_put(node->key, node);

	total_elements++;
#endif
}

void cache_update (const char *key, const char *val, uint64_t vpage, uint32_t num_pages)
{
#if ENABLE_CACHE
	struct cached_node *node = index_get(key);

//	printk("CACHE: update %s %s \n", key, val);
	if (!node) {
		cache_add(key, val, vpage, num_pages);
		return;
	}

	vfree(node->val);
	node->val = vmalloc(strlen(val) + 1);
	strncpy(node->val, val, strlen(val) + 1);

	node->vpage = vpage;
	node->num_pages = num_pages;

	list_del(&node->list);

	list_add_tail(&node->list, &cache_list);
#endif
}


int cache_lookup(const char *key, char *val, uint64_t *vpage, uint32_t *num_pages)
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

//	if (val)
//		printk("CACHE: LOOKUP key %s val %s \n", key, val);
	return 1;
#else
	return 0;
#endif
}

void cache_remove(const char *key)
{
#if ENABLE_CACHE
	struct cached_node *node = index_get(key);

	if (!node) {
		return;
	}

	list_del(&node->list);

//		printk("CACHE: REMOVE key %s val %s \n", node->key, node->val);
	index_delete(node->key);

	vfree(node->key);
	vfree(node->val);
	vfree(node);

	total_elements--;
#endif
}

void cache_clean(void)
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
