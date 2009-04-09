/*
 * kernel/power/user_suspend_blocker.c
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
#include <linux/suspend.h>
#include <linux/suspend_ioctls.h>

enum {
	DEBUG_FAILURE	= BIT(0),
};
static int debug_mask = DEBUG_FAILURE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

static DEFINE_MUTEX(ioctl_lock);

#define USER_SUSPEND_BLOCKER_NAME_LEN 31

struct user_suspend_blocker {
	struct suspend_blocker	blocker;
	char			name[USER_SUSPEND_BLOCKER_NAME_LEN + 1];
	bool			registered;
};

static int user_suspend_blocker_open(struct inode *inode, struct file *filp)
{
	struct user_suspend_blocker *blocker;

	blocker = kzalloc(sizeof(*blocker), GFP_KERNEL);
	if (!blocker)
		return -ENOMEM;

	nonseekable_open(inode, filp);
	strcpy(blocker->name, "(userspace)");
	blocker->blocker.name = blocker->name;
	filp->private_data = blocker;

	return 0;
}

static int suspend_blocker_set_name(struct user_suspend_blocker *blocker,
				    void __user *name, size_t name_len)
{
	if (blocker->registered)
		return -EBUSY;

	if (name_len > USER_SUSPEND_BLOCKER_NAME_LEN)
		name_len = USER_SUSPEND_BLOCKER_NAME_LEN;

	if (copy_from_user(blocker->name, name, name_len))
		return -EFAULT;
	blocker->name[name_len] = '\0';

	return 0;
}

static long user_suspend_blocker_ioctl(struct file *filp, unsigned int cmd,
					unsigned long _arg)
{
	void __user *arg = (void __user *)_arg;
	struct user_suspend_blocker *blocker = filp->private_data;
	long ret = 0;

	mutex_lock(&ioctl_lock);
	if ((cmd & ~IOCSIZE_MASK) == SUSPEND_BLOCKER_IOCTL_SET_NAME(0)) {
		ret = suspend_blocker_set_name(blocker, arg, _IOC_SIZE(cmd));
		goto done;
	}
	if (!blocker->registered) {
		suspend_blocker_register(&blocker->blocker);
		blocker->registered = true;
	}
	switch (cmd) {
	case SUSPEND_BLOCKER_IOCTL_BLOCK:
		suspend_block(&blocker->blocker);
		break;

	case SUSPEND_BLOCKER_IOCTL_UNBLOCK:
		suspend_unblock(&blocker->blocker);
		break;

	default:
		ret = -ENOTTY;
	}
done:
	if (ret && (debug_mask & DEBUG_FAILURE))
		pr_err("user_suspend_blocker_ioctl: cmd %x failed, %ld\n",
			cmd, ret);
	mutex_unlock(&ioctl_lock);
	return ret;
}

static int user_suspend_blocker_release(struct inode *inode, struct file *filp)
{
	struct user_suspend_blocker *blocker = filp->private_data;

	if (blocker->registered)
		suspend_blocker_unregister(&blocker->blocker);
	kfree(blocker);

	return 0;
}

const struct file_operations user_suspend_blocker_fops = {
	.open = user_suspend_blocker_open,
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
