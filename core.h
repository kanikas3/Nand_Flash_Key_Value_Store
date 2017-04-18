/**
 * Header for the protoype main file
 */

#ifndef LKP_KV_H
#define LKP_KV_H

#include <linux/mtd/mtd.h>
#include <linux/semaphore.h>

/* state for a flash block: used or free */
typedef enum {
	BLK_FREE,
	BLK_USED
} blk_state;

/* state for a flash page: used or free */
typedef enum {
	PG_FREE,
	PG_VALID
} page_state;

/* data structure containing the state of a flash block and the state of the 
 * flash pages it contains */
typedef struct {
	blk_state state;
	page_state *pages_states;
} blk_info;

/* global attributes for our system */
typedef struct {
	struct mtd_info *mtd;	/* pointer to the used flash partition mtd_info object */
	int mtd_index;		/* the partition index */
	int nb_blocks;		/* amount of managed flash blocks */
	int block_size;		/* flash bock size in bytes */
	int page_size;		/* flash page size in bytes */
	int current_block;	/* current flash block we write to */
	int current_page_offset;	/* index of the next flash page to write in the current block */
	int pages_per_block;	/* number of flash pages per block */
	blk_info *blocks;	/* metadata : flash blocks/pages state */
	int format_done;	/* used during format operation */
	int read_only;		/* are we in read-only mode? */
	struct semaphore format_lock;	/* used during the format operation */

} lkp_kv_cfg;

/* export some prototypes for function used in the virtual device file */
int set_keyval(const char *key, const char *val);
int get_keyval(const char *key, char *val);
int format(void);
int read_page(int page_index, char *buf, lkp_kv_cfg *config);
int write_page(int page_index, const char *buf, lkp_kv_cfg *config);
int read_bytes (int page_index, char *buf, lkp_kv_cfg *config,
			size_t bytes);

extern lkp_kv_cfg data_config;

#endif /* LKP_KV_H */
