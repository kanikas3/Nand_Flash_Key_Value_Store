#ifndef PROJECT6_CACHE_H
#define PROJECT6_CACHE_H

/**
 * @brief Structure of the node in the double link list
 */
struct cached_node
{
    uint64_t vpage;

    uint32_t num_pages;

    char *key;

    char *val;

    struct list_head list;
};

/**
 * @brief Structure of the node in the hash table
 */
struct index_item {
     const char* key;

     struct cached_node *ptr;

     struct hlist_node hlist_elem;
};

/**
 * @brief Deletes the entire cache
 */
void project6_cache_clean(void);

/**
 * @brief Removes a key from the cache
 *
 * @param key The key to be removed
 */
void project6_cache_remove(const char *key);

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
int project6_cache_lookup(const char *key,
			  char *val, uint64_t *vpage, uint32_t *num_pages);

/**
 * @brief Update the existing entry in cache/add new entry
 *
 * @param key Key to be updated
 * @param val Value of the key which is updated
 * @param vpage vpage for the key
 * @param num_pages Num_pages corresponding to the vpage
 */
void project6_cache_update (const char *key,
			    const char *val,
			    uint64_t vpage, uint32_t num_pages);

/**
 * @brief Adds the given key, val into the LRU Cache
 *
 * @param key Key to be added
 * @param val Value to be added
 * @param vpage Vpage corresponding to the key
 * @param num_pages Num_pages for the key,val
 */
void project6_cache_add (const char *key,
			 const char *val, uint64_t vpage, uint32_t num_pages);

#endif
