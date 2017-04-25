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

void
index_put(const char* key, struct cached_node *ptr)
{
	struct index_item *node;
	node = vmalloc(sizeof(*node));
	if(!node)
		printk(KERN_INFO "Could not put key in index");

	node->key = key;
	node->ptr = ptr;

	hlist_add_head(&node->hlist_elem, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))]);
}


void
index_delete(const char *key)
{
	struct index_item *node;
	struct hlist_node *tmp;
	hlist_for_each_entry_safe(node, tmp, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		hlist_del(&node->hlist_elem);
		vfree(node);
	}
}

void index_clear(void)
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

void *
index_get(const char* key)
{
	struct index_item *node;
	hlist_for_each_entry(node, &kv_index_table[hash_string(key, HASH_SIZE(kv_index_table))], hlist_elem)
	{
		if(strcmp(node->key, key) == 0)
			return node->ptr;
	}
	return NULL;
}

void cache_evict (void)
{
	struct cached_node *node = list_first_entry(&cache_list, struct cached_node, list);

	list_del(&node->list);

	index_delete(node->key);

	vfree(node->key);
	vfree(node->val);
	vfree(node);

	total_elements--;
}

void cache_add (const char *key, const char *val, uint64_t vpage, uint32_t num_pages)
{

	struct cached_node *node = vmalloc(sizeof(struct cached_node));

	if (!node) {
		printk("Node allocation failed for caching \n");
		return;
	}

	node->key = vmalloc(strlen(key) + 1);
	node->val = vmalloc(strlen(val) + 1);

	strncpy(node->key, key, strlen(key));
	strncpy(node->val, val, strlen(val));

	node->vpage = vpage;
	node->num_pages = num_pages;

	if (total_elements == NUM_CACHE_PAGES) {
		cache_evict();
	}

	list_add_tail(&node->list, &cache_list);

	index_put(node->key, node);

	total_elements++;
}

void cache_update (const char *key, const char *val, uint64_t vpage, uint32_t num_pages)
{
	struct cached_node *node = index_get(key);

	if (!node) {
		cache_add(key, val, vpage, num_pages);
		return;
	}

	vfree(node->val);
	node->val = vmalloc(strlen(val) + 1);
	strncpy(node->val, val, strlen(val));

	node->vpage = vpage;
	node->num_pages = num_pages;

	list_del(&node->list);

	list_add_tail(&node->list, &cache_list);
}


int cache_lookup(const char *key, char *val, uint64_t *vpage, uint64_t *num_pages)
{
	struct cached_node *node = index_get(key);

	if (!node) {
		return -1;
	}

	strcpy(val, node->val);

	*vpage = node->vpage;
	*num_pages = node->num_pages;

	return 0;
}

void cache_remove(const char *key)
{
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
}

void cache_clean(void)
{
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
}
