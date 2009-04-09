/* include/linux/suspend_block_dev.h
 *
 * Copyright (C) 2009 Google, Inc.
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

#ifndef _LINUX_SUSPEND_BLOCK_DEV_H
#define _LINUX_SUSPEND_BLOCK_DEV_H

#include <linux/ioctl.h>

#define SUSPEND_BLOCKER_IOCTL_INIT(len)		_IOC(_IOC_WRITE, 's', 0, len)
#define SUSPEND_BLOCKER_IOCTL_BLOCK		_IO('s', 1)
#define SUSPEND_BLOCKER_IOCTL_UNBLOCK		_IO('s', 2)
#define SUSPEND_BLOCKER_IOCTL_BLOCK_TIMEOUT	_IOW('s', 3, struct timespec)

#endif
