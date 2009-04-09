/* kernel/power/user_suspend_block.c
 *
 * Copyright (C) 2009-2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/suspend_blocker.h>
#include <linux/suspend_block_dev.h>

enum {
	DEBUG_FAILURE	= BIT(0),
};
static int debug_mask = DEBUG_FAILURE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

static DEFINE_MUTEX(ioctl_lock);

struct user_suspend_blocker {
	struct suspend_blocker	blocker;
	char			name[0];
};

static int create_user_suspend_blocker(struct file *file, void __user *name,
				 size_t name_len)
{
	struct user_suspend_blocker *bl;
	if (file->private_data)
		return -EBUSY;
	if (name_len > NAME_MAX)
		return -ENAMETOOLONG;
	bl = kzalloc(sizeof(*bl) + name_len + 1, GFP_KERNEL);
	if (!bl)
		return -ENOMEM;
	if (copy_from_user(bl->name, name, name_len))
		goto err_fault;
	suspend_blocker_init(&bl->blocker, bl->name);
	file->private_data = bl;
	return 0;

err_fault:
	kfree(bl);
	return -EFAULT;
}

static long user_suspend_blocker_ioctl(struct file *file, unsigned int cmd,
				unsigned long _arg)
{
	void __user *arg = (void __user *)_arg;
	struct user_suspend_blocker *bl;
	long ret;

	mutex_lock(&ioctl_lock);
	if ((cmd & ~IOCSIZE_MASK) == SUSPEND_BLOCKER_IOCTL_INIT(0)) {
		ret = create_user_suspend_blocker(file, arg, _IOC_SIZE(cmd));
		goto done;
	}
	bl = file->private_data;
	if (!bl) {
		ret = -ENOENT;
		goto done;
	}
	switch (cmd) {
	case SUSPEND_BLOCKER_IOCTL_BLOCK:
		suspend_block(&bl->blocker);
		ret = 0;
		break;
	case SUSPEND_BLOCKER_IOCTL_UNBLOCK:
		suspend_unblock(&bl->blocker);
		ret = 0;
		break;
	default:
		ret = -ENOTSUPP;
	}
done:
	if (ret && debug_mask & DEBUG_FAILURE)
		pr_err("user_suspend_blocker_ioctl: cmd %x failed, %ld\n",
			cmd, ret);
	mutex_unlock(&ioctl_lock);
	return ret;
}

static int user_suspend_blocker_release(struct inode *inode, struct file *file)
{
	struct user_suspend_blocker *bl = file->private_data;
	if (!bl)
		return 0;
	suspend_blocker_destroy(&bl->blocker);
	kfree(bl);
	return 0;
}

const struct file_operations user_suspend_blocker_fops = {
	.release = user_suspend_blocker_release,
	.unlocked_ioctl = user_suspend_blocker_ioctl,
};

struct miscdevice user_suspend_blocker_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "suspend_blocker",
	.fops = &user_suspend_blocker_fops,
};

static int __init user_suspend_blocker_init(void)
{
	return misc_register(&user_suspend_blocker_device);
}

static void __exit user_suspend_blocker_exit(void)
{
	misc_deregister(&user_suspend_blocker_device);
}

module_init(user_suspend_blocker_init);
module_exit(user_suspend_blocker_exit);
