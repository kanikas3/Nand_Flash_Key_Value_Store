#ifndef LKP_KV_H
#define LKP_KV_H

#include <linux/mtd/mtd.h>
#include <linux/semaphore.h>

/* Markers for the key */
#define NEW_KEY 0x20000000
#define PREVIOUS_KEY 0x10000000

/* Vpage status */
#define PAGE_UNALLOCATED 0xFFFFFFFFFFFFFFFF
#define PAGE_GARBAGE_RECLAIMED 0x8FFFFFFFFFFFFFFF

/* The below status are for ppage */

/* Not stored on flash */
#define PAGE_NOT_MAPPED 0x0

/* Below 3 are stored on flash */
#define PAGE_INVALID 0x1
#define PAGE_VALID 0x2
#define PAGE_FREE 0x3

/* Not stored on flash */
#define PAGE_RECLAIMED 0x4

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

extern project6_cfg data_config;
extern project6_cfg meta_config;
extern uint8_t *page_buffer;
extern uint64_t total_written_page;
extern uint8_t *bitmap;
extern uint64_t *mapper;

/**
 * @brief Performs set/update of key
 *
 * @param key Key to be updated/set
 * @param val Value for the given key
 *
 * @return 0 for success, -1 for failure
 */
int set_keyval(const char *key, const char *val);

/**
 * @brief Gets the value for given key
 *
 * @param key Key to be searched
 * @param val Pointer for the value
 *
 * @return 0 for success, -1 for failure
 */
int get_keyval(const char *key, char *val);

/**
 * @brief Deletes the given key
 *
 * @param key String for the key
 *
 * @return 0 on success, -1 for failure
 */
int del_keyval(const char *key);

/**
 * @brief Performs format of the disk
 *
 * @return 0 for success, otherwise appropriate error codes
 */
int format(void);

/**
 * @brief Reads a page from the flash
 *
 * @param page_index Index of the page
 * @param buf Buffer where we need to read
 * @param config Config for the partition
 *
 * @return 0 for success, otherwise appropriate error code
 */
int read_page(int page_index, char *buf, project6_cfg *config);

/**
 * @brief Writes a page to the flash
 *
 * @param page_index Index of the page
 * @param buf Buffer where we need to read
 * @param config Config for the partition
 *
 * @return 0 for success, otherwise appropriate error code
 */
int write_page(int page_index, const char *buf, project6_cfg *config);

/**
 * @brief Performs erase of the given block
 *
 * @param block_index Index of the block to erase
 * @param block_count Number of blocks to be erased
 * @param config Config of the partition
 * @param callback Callback to be called for erase
 *
 * @return -1 for failure, 0 for success
 */
int erase_block(uint64_t block_index, int block_count,
		project6_cfg *config, void (*callback)(struct erase_info *e));

/**
 * @brief Creates a mapping in a block different than given block, it is used by
 * the garbage collection to find a mapping in the new block
 *
 * @param vpage vpage for which we need mapping
 * @param ppage Returns the physical page into this pointer
 * @param blk_number Block number which must be avoid while providing mapping
 *
 * @return 0 on success, otherwise appropriate error code
 */
int project6_create_mapping_new_block(uint64_t vpage,
				      uint64_t *ppage, uint64_t block_counter);

/**
 * @brief Gets  the existing mapping for the given vpage
 *
 * @param vpage Vpage whose mapping is found
 * @param ppage Ppage pointer to be filled
 *
 * @return Appropriate codes based on state of vpage
 */
int project6_get_existing_mapping(uint64_t vpage, uint64_t *ppage);

/**
 * @brief Marks the vpage invalid
 *
 * @param vpage Vpage to be marked invalid
 * @param num_pages Number of pages to be marked invalid
 *
 * @return 0 on success, otherwise -EPERM
 */
int project6_mark_vpage_invalid(uint64_t vpage, uint64_t num_pages);

/**
 * @brief Starts Garbage Collection
 *
 * @param threshold TotalPages/Threshold is the marker when garbage collection
 * starts
 *
 * @return Appropriate Error code, 0 on success
 */
int project6_garbage_collection(int threshold);

/**
 * @brief Create mapping for multiple pages
 *
 * @param vpage Virtual page to be mapped
 * @param num_pages Number of pages to be mapped
 *
 * @return 0 on success, otherwise appropriate error code
 */
int project6_create_mapping_multipage(uint64_t vpage, uint32_t num_pages);

/**
 * @brief Finds a free page from the given page
 *
 * @param ppage Offset from which next free page is found
 */
void project6_fix_free_page_pointer(uint64_t ppage);

/**
 * @brief Callback for datapartition erase operation
 *
 * @param e Pointer to erase info structure
 */
void data_format_callback(struct erase_info *e);

/**
 * @brief Callback for metadata partition erase operation
 *
 * @param e Pointer to erase info structure
 */
void metadata_format_callback(struct erase_info *e);

/**
 * @brief Get existing state for the physical page
 *
 * @param ppage Physical page number
 *
 * @return state of the page
 */
uint8_t project6_get_ppage_state(uint64_t ppage);

/**
 * @brief Set the state for the physical page
 *
 * @param ppage Physical page number
 * @param state State to be set
 */
void project6_set_ppage_state(uint64_t ppage, uint8_t state);

/**
 * @brief Creates a new metadata from scratch
 *
 * @param meta_config Configuration of the meta-data
 * @param block_num block used to create the meta-data
 *
 * @return returns 0 on success, otherwise appropriate error code
 */
int project6_create_meta_data(project6_cfg *meta_config, uint32_t block_num);

/**
 * @brief Construct the in memory meta-data
 *
 * @param meta_config Config Pointer of meta-data partition
 * @param data_config Config Pointer of data partition
 * @param read_disk Read from disk or perform default meta-data construction
 *
 * @return 0 for success, otherwise appropriate error code
 */
int project6_construct_meta_data(project6_cfg *meta_config,
			project6_cfg *data_config,
			bool read_disk);

/**
 * @brief Flush the meta-data back to flash
 *
 * @param config Config of the meta-data
 */
void project6_flush_meta_data_to_flash(project6_cfg *config);

/**
 * @brief Flush the meta-data on periodic basis
 */
void project6_flush_meta_data_timely(void);
#endif /* LKP_KV_H */
