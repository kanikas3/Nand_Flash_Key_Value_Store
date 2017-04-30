/**
 * Header for the protoype main file
 */

#ifndef LKP_KV_H
#define LKP_KV_H

#include <linux/mtd/mtd.h>
#include <linux/semaphore.h>

#define PAGE_UNALLOCATED 0xFFFFFFFFFFFFFFFF
#define PAGE_GARBAGE_RECLAIMED 0x8FFFFFFFFFFFFFFF

#define PAGE_NOT_MAPPED 0x0
#define PAGE_INVALID 0x1
#define PAGE_VALID 0x2
#define PAGE_FREE 0x3
#define PAGE_RECLAIMED 0x4

typedef struct free_list_t {
	uint64_t ppage;
	struct list_head list;
} free_list;

/* global attributes for our system */
typedef struct {
	struct mtd_info *mtd;	/* pointer to the used flash partition mtd_info object */
	int mtd_index;		/* the partition index */
	int nb_blocks;		/* amount of managed flash blocks */
	int block_size;		/* flash bock size in bytes */
	int page_size;		/* flash page size in bytes */
	int pages_per_block;	/* number of flash pages per block */
	int format_done;	/* used during format operation */
	int read_only;		/* are we in read-only mode? */
	struct semaphore format_lock;	/* used during the format operation */

} project6_cfg;

/* export some prototypes for function used in the virtual device file */
int set_keyval(const char *key, const char *val);
int get_keyval(const char *key, char *val);
int del_keyval(const char *key);
int format(void);
int read_page(int page_index, char *buf, project6_cfg *config);
int write_page(int page_index, const char *buf, project6_cfg *config);

int project6_create_mapping_new_block(uint64_t vpage, uint64_t *ppage, uint64_t block_counter);

int project6_get_existing_mapping(uint64_t vpage, uint64_t *ppage);

int project6_mark_vpage_invalid(uint64_t vpage, uint64_t num_pages);

int erase_block(uint64_t block_index, int block_count, project6_cfg *config, void (*callback)(struct erase_info *e));

int project6_garbage_collection(int threshold);

int project6_create_mapping_multipage(uint64_t vpage, uint32_t num_pages);

void project6_cache_clean(void);

void project6_cache_remove(const char *key);

int project6_cache_lookup(const char *key, char *val, uint64_t *vpage, uint32_t *num_pages);

void project6_cache_update (const char *key, const char *val, uint64_t vpage, uint32_t num_pages);

void project6_cache_add (const char *key, const char *val, uint64_t vpage, uint32_t num_pages);

void project6_fix_free_page_pointer(uint64_t ppage);

void data_format_callback(struct erase_info *e);

void metadata_format_callback(struct erase_info *e);

uint8_t project6_get_ppage_state(uint64_t ppage);

void project6_set_ppage_state(uint64_t ppage, uint8_t state);

int project6_create_meta_data(project6_cfg *meta_config);

int project6_construct_meta_data(project6_cfg *meta_config,
			project6_cfg *data_config,
			bool read_disk);

void project6_flush_meta_data_to_flash(project6_cfg *config);

extern project6_cfg data_config;
extern uint8_t *page_buffer;
extern uint64_t total_written_page;
extern uint8_t *bitmap;
extern uint64_t *mapper;

#endif /* LKP_KV_H */
