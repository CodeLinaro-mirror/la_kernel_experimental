/* include/linux/wakelock.h
 *
 * Copyright (C) 2007-2008 Google, Inc.
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

#ifndef _LINUX_WAKELOCK_H
#define _LINUX_WAKELOCK_H

#include <linux/suspend.h>

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_IDLE,    /* Prevent low power idle */
	WAKE_LOCK_TYPE_COUNT
};

struct wake_lock {
	struct suspend_blocker blocker;
	int type;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
					const char *name)
{
	lock->type = type;
	if (type == WAKE_LOCK_SUSPEND)
		suspend_blocker_init(&lock->blocker, name);
	else
		WARN_ON(1);
}
static inline void wake_lock_destroy(struct wake_lock *lock)
{
	if (lock->type == WAKE_LOCK_SUSPEND)
		suspend_blocker_unregister(&lock->blocker);
}
static inline void wake_lock(struct wake_lock *lock)
{
	if (lock->type == WAKE_LOCK_SUSPEND)
		suspend_block(&lock->blocker);
}
static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	if (lock->type == WAKE_LOCK_SUSPEND)
		suspend_block_timeout(&lock->blocker, timeout);
}
static inline void wake_unlock(struct wake_lock *lock)
{
	if (lock->type == WAKE_LOCK_SUSPEND)
		suspend_unblock(&lock->blocker);
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	if (lock->type == WAKE_LOCK_SUSPEND)
		return suspend_blocker_is_active(&lock->blocker);
	return 0;
}
static inline long has_wake_lock(int type)
{
	if (type == WAKE_LOCK_SUSPEND)
		return suspend_is_blocked();
	return 0;
}

#endif

