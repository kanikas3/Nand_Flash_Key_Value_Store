/**
 * Header file for virtual device management
 */

#ifndef LKP_KV_DEVICE_H
#define LKP_KV_DEVICE_H

#include <linux/ioctl.h>

/* virtual device major number */
#define MAJOR_NUM 100

/* virtual device name */
#define DEVICE_NAME "/dev/lkp_kv"

/* data structure representing a key/value couple as well as a return code
 * indicating the fact the a read/write operation has been successful or 
 * not
 */
typedef struct {
	char *key;
	char *val;
	int key_len;
	int val_len;
	int status;
} keyval;

/* The 3 ioctl commands that can be sent to the virtual device: read operation 
 * (get), write operation (set) and format operation. The 3rd parameter 
 * represents the parameter that is passed when the ioctl command is called: 
 * for get and set it is a keyval object (see above), and for the format 
 * operation it is just an integer return code indicating that the operation
 * completed successfully or not
 */
#define IOCTL_GET _IOR(MAJOR_NUM, 0, keyval *)
#define IOCTL_SET _IOR(MAJOR_NUM, 1, keyval *)
#define IOCTL_FORMAT _IOR(MAJOR_NUM, 2, int *)

int device_init(void);
void device_exit(void);

#endif /* LKP_KV_DEVICE_H */
