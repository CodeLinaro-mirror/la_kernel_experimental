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

#include <linux/pm_qos_params.h>
#include <linux/suspend.h>
#include <linux/timer.h>

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_IDLE,    /* Prevent low power idle */
	WAKE_LOCK_TYPE_COUNT
};

struct wake_lock {
	union {
		struct suspend_blocker blocker;
		struct {
			char *name;
			struct timer_list timer;
		} qos;
	};
	int type;
};

static inline void wake_unlock(struct wake_lock *lock)
{
	switch(lock->type) {
	case WAKE_LOCK_SUSPEND:
		suspend_unblock(&lock->blocker);
		break;
	case WAKE_LOCK_IDLE:
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
					  lock->qos.name, PM_QOS_DEFAULT_VALUE);
		break;
	}		
}

static inline void idle_wake_lock_timeout(unsigned long data)
{
	wake_unlock((struct wake_lock *)data);
}

static inline void wake_lock_init(struct wake_lock *lock, int type,
					const char *name)
{
	lock->type = type;
	switch(type) {
	case WAKE_LOCK_SUSPEND:
		suspend_blocker_init(&lock->blocker, name);
		break;
	case WAKE_LOCK_IDLE:
		lock->qos.name = (char *)name;
		setup_timer(&lock->qos.timer, idle_wake_lock_timeout,
			    (unsigned long)lock);
		pm_qos_add_requirement(PM_QOS_CPU_DMA_LATENCY, lock->qos.name,
				       PM_QOS_DEFAULT_VALUE);
		break;
	default:
		WARN_ON(1);
	}
}
static inline void wake_lock_destroy(struct wake_lock *lock)
{
	switch(lock->type) {
	case WAKE_LOCK_SUSPEND:
		suspend_blocker_unregister(&lock->blocker);
		break;
	case WAKE_LOCK_IDLE:
		del_timer(&lock->qos.timer);
		pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY,
					  lock->qos.name);
		break;
	}
}
static inline void wake_lock(struct wake_lock *lock)
{
	switch(lock->type) {
	case WAKE_LOCK_SUSPEND:
		suspend_block(&lock->blocker);
		break;
	case WAKE_LOCK_IDLE:
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
					  lock->qos.name, 0);
		break;
	}		
}
static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	switch(lock->type) {
	case WAKE_LOCK_SUSPEND:
		suspend_block_timeout(&lock->blocker, timeout);
		break;
	case WAKE_LOCK_IDLE:
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY,
					  lock->qos.name, 0);
		mod_timer(&lock->qos.timer, jiffies + timeout);
		break;
	}
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	switch(lock->type) {
	case WAKE_LOCK_SUSPEND:
		return suspend_blocker_is_active(&lock->blocker);
	default:
		WARN_ON(1);
		return 0;
	}
}
static inline long has_wake_lock(int type)
{
	switch(type) {
	case WAKE_LOCK_SUSPEND:
		return suspend_is_blocked();
	case WAKE_LOCK_IDLE:
		return !pm_qos_requirement(PM_QOS_CPU_DMA_LATENCY);
	default:
		WARN_ON(1);
		return 0;
	}
}

#endif

