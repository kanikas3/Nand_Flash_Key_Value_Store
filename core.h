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

} lkp_kv_cfg;

/* export some prototypes for function used in the virtual device file */
int set_keyval(const char *key, const char *val);
int get_keyval(const char *key, char *val);
int del_keyval(const char *key);
int format(void);
int read_page(int page_index, char *buf, lkp_kv_cfg *config);
int write_page(int page_index, const char *buf, lkp_kv_cfg *config);

int create_mapping(uint64_t vpage, uint64_t *ppage);

int create_mapping_new_block(uint64_t vpage, uint64_t *ppage, uint64_t block_counter);

int get_existing_mapping(uint64_t vpage, uint64_t *ppage);

int mark_vpage_invalid(uint64_t vpage, uint64_t num_pages);

int erase_block(uint64_t block_index, int block_count, lkp_kv_cfg *config);

int garbage_collection(int threshold);

int create_mapping_multipage(uint64_t vpage, uint32_t num_pages);

extern lkp_kv_cfg data_config;
extern uint8_t *page_buffer;

#endif /* LKP_KV_H */
