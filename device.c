/*
 * Defines the IOCTLS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "device.h"

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>

#include "core.h"

/* global attributes for the virtual device */
static atomic_t file_is_open;

/**
 * called when a process opens the virtual device file 
 * i.e. open("/dev/lkp_kv")
 */
static int device_open(struct inode *inode, struct file *file)
{
	/* only a single process can open the device at a time */
	int is_open = atomic_read(&file_is_open);
	if (is_open)
		return -EBUSY;

	atomic_set(&file_is_open, 1);

	return 0;
}

/**
 * function called when the virtual device file is closed
 */
static int device_release(struct inode *inode, struct file *file)
{
	atomic_set(&file_is_open, 0);
	return 0;
}

/**
 * ioctl reception. In simplicity order, first study format, then get, 
 * then set
 */
static long device_ioctl(struct file *file, unsigned int ioctl_num,
			 unsigned long ioctl_param)
{
	switch (ioctl_num) {
		/* format operation */
	case IOCTL_FORMAT:
		{
			int ret;
			/* call module core function */
			ret = format();

			/* copy return code to userspace */
			put_user(ret, (int *)ioctl_param);
			break;
		}

	case IOCTL_DEL:
		{
			keyt skey;
			int err_bytes_copied = 0;
			int ret = 0;
			char *key;

			/* get the keyval structure from userspace
			 * warning: character pointers in the struct still point
			 * to userspace data */
			err_bytes_copied +=
			    copy_from_user(&skey, (void *)ioctl_param,
					   sizeof(keyt));

			key = (char *)vmalloc((skey.key_len + 1) * sizeof(char));

			if (!key) {
				ret = -1;
				put_user(ret,
					 (int *)&(((keyt *) (ioctl_param))->status));
				break;
			}

			/* now that we have the character string sizes, we get get the
			 * string themselves */
			err_bytes_copied +=
			    copy_from_user(key, skey.key, skey.key_len + 1);

			if (!err_bytes_copied)
				ret = del_keyval(key);	/* call module core function */
			else
				ret = -1;

			put_user(ret,
				 (int *)&(((keyt *) (ioctl_param))->status));

			vfree(key);

			break;
		}

		/* set operation */
	case IOCTL_SET:
		{
			int ret = 0;
			int err_bytes_copied = 0;
			char *key, *val;
			keyval kv;

			/* get the keyval structure from userspace
			 * warning: character pointers in the struct still point
			 * to userspace data */
			err_bytes_copied +=
			    copy_from_user(&kv, (void *)ioctl_param,
					   sizeof(keyval));

			key = (char *)vmalloc((kv.key_len + 1) * sizeof(char));

			if (!key) {
				ret = -1;
				put_user(ret,
					 (int *)&(((keyval *) (ioctl_param))->status));
				break;
			}

			val = (char *)vmalloc((kv.val_len + 1) * sizeof(char));

			if (!val) {
				ret = -1;
				vfree(key);
				put_user(ret,
					 (int *)&(((keyval *) (ioctl_param))->status));
				break;
			}

			/* now that we have the character string sizes, we get get the
			 * string themselves */
			err_bytes_copied +=
			    copy_from_user(key, kv.key, kv.key_len + 1);
			err_bytes_copied +=
			    copy_from_user(val, kv.val, kv.val_len + 1);

			if (!err_bytes_copied)
				ret = set_keyval(key, val);	/* call module core function */
			else
				ret = -1;

			/* copy return code to userspace */
			put_user(ret,
				 (int *)&(((keyval *) (ioctl_param))->status));

			/* nettoyage */
			vfree(val);
			vfree(key);
			break;
		}

		/* get operation */
	case IOCTL_GET:
		{
			int ret = 0;
			int err_bytes_copied = 0;
			char *key, *val;
			keyval kv;

			/* get the keyval struct */
			err_bytes_copied +=
			    copy_from_user(&kv, (void *)ioctl_param,
					   sizeof(keyval));

			key = (char *)vmalloc((kv.key_len + 1) * sizeof(char));

			if (!key) {
				ret = -1;
				put_user(ret,
					 (int *)&(((keyval *) (ioctl_param))->status));
				break;
			}

			val =
			    (char *)vmalloc(4 * (data_config.page_size)
					    * sizeof(char));
			if (!val) {
				ret = -1;
				vfree(key);
				put_user(ret,
					 (int *)&(((keyval *) (ioctl_param))->status));
				break;
			}
			/* get the key */
			err_bytes_copied +=
			    copy_from_user(key, kv.key, kv.key_len + 1);

			if (!err_bytes_copied) {
				ret = get_keyval(key, val);	/* appel au coeur du module */
				if (ret >= 0) {
					/* write the result to userspace */
					err_bytes_copied +=
					    copy_to_user(kv.val, val,
							 strlen(val) + 1);
				}
			}

			if (err_bytes_copied)
				ret = -1;

			/* copy return code to userspace */
			put_user(ret,
				 (int *)&(((keyval *) (ioctl_param))->status));

			vfree(val);
			vfree(key);

			break;
		}

	default:
		return -8;	/* bad ioctl code */
	}

	return 0;
}

/* functions to manipulate the virtual device file */
struct file_operations Fops = {
	.unlocked_ioctl = device_ioctl,
	.open = device_open,
	.release = device_release,
};

/* Virtual device initialization, called from the module init function
 */
int device_init(void)
{
	int ret;

	atomic_set(&file_is_open, 0);

	/* virtual device creation */
	ret = register_chrdev(MAJOR_NUM, DEVICE_NAME, &Fops);
	if (ret < 0)
		return -1;

	return 0;
}

/**
 * virtual device deletion, called from the module exit function
 */
void device_exit(void)
{
	unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
}
