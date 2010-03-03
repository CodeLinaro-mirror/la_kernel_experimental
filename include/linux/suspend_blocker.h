/* include/linux/suspend_blocker.h
 *
 * Copyright (C) 2007-2010 Google, Inc.
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

#ifndef _LINUX_SUSPEND_BLOCKER_H
#define _LINUX_SUSPEND_BLOCKER_H

#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>

/**
 * struct suspend_blocker_stats - statistics for a suspend blocker
 *
 * @count: Number of times this blocker has been deacivated.
 * @wakeup_count: Number of times this blocker was the first to block suspend
 *	after resume.
 * @total_time: Total time this suspend blocker has prevented suspend.
 * @prevent_suspend_time: Time this suspend blocker has prevented suspend while
 *	user-space requested suspend.
 * @max_time: Max time this suspend blocker has been continuously active.
 * @last_time: Monotonic clock when the active state last changed.
 */
struct suspend_blocker_stats {
#ifdef CONFIG_SUSPEND_BLOCKER_STATS
	unsigned int count;
	unsigned int wakeup_count;
	ktime_t total_time;
	ktime_t prevent_suspend_time;
	ktime_t max_time;
	ktime_t last_time;
#endif
};

/**
 * struct suspend_blocker - the basic suspend_blocker structure
 * @link: List entry for active or inactive list.
 * @flags: Tracks initialized and active state and statistics.
 * @name: Suspend blocker name used for debugging.
 *
 * When a suspend_blocker is active it prevents the system from entering
 * opportunistic suspend.
 *
 * The suspend_blocker structure must be initialized by suspend_blocker_init()
 */
struct suspend_blocker {
#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
	struct list_head link;
	int flags;
	const char *name;
	struct suspend_blocker_stats stat;
#endif
};

/**
 * struct suspend_blocking_work - the basic suspend_blocking_work structure
 * @work:		Standard work struct.
 * @suspend_blocker:	Suspend blocker.
 * @func:		Callback.
 * @lock:		Spinlock protecting pending and running state.
 * @active:		Number of cpu workqueues where work is pending or
 *			callback is running.
 *
 * When suspend blocking work is pending or its callback is running it prevents
 * the system from entering opportunistic suspend.
 *
 * The suspend_blocking_work structure must be initialized by
 * suspend_blocking_work_init().
 */

struct suspend_blocking_work {
	struct work_struct work;
#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
	struct suspend_blocker suspend_blocker;
	work_func_t func;
	spinlock_t lock;
	int active;
#endif
};

static inline struct suspend_blocking_work *to_suspend_blocking_work(
	struct work_struct *work)
{
	return container_of(work, struct suspend_blocking_work, work);
}

#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
#define __SUSPEND_BLOCKER_INITIALIZER(blocker_name) \
	{ .name = #blocker_name, }

#define DEFINE_SUSPEND_BLOCKER(blocker, name) \
	struct suspend_blocker blocker = __SUSPEND_BLOCKER_INITIALIZER(name)

extern void suspend_blocker_register(struct suspend_blocker *blocker);
extern void suspend_blocker_init(struct suspend_blocker *blocker,
				 const char *name);
extern void suspend_blocker_unregister(struct suspend_blocker *blocker);
extern void suspend_block(struct suspend_blocker *blocker);
extern void suspend_unblock(struct suspend_blocker *blocker);
extern bool suspend_blocker_is_active(struct suspend_blocker *blocker);
extern bool suspend_is_blocked(void);

void suspend_blocking_work_init(struct suspend_blocking_work *work,
				work_func_t func, const char *name);
void suspend_blocking_work_destroy(struct suspend_blocking_work *work);
int queue_suspend_blocking_work(struct workqueue_struct *wq,
				struct suspend_blocking_work *work);
int schedule_suspend_blocking_work(struct suspend_blocking_work *work);
int cancel_suspend_blocking_work_sync(struct suspend_blocking_work *work);

#else

#define DEFINE_SUSPEND_BLOCKER(blocker, name) \
	struct suspend_blocker blocker

static inline void suspend_blocker_register(struct suspend_blocker *bl) {}
static inline void suspend_blocker_init(struct suspend_blocker *bl,
					const char *n) {}
static inline void suspend_blocker_unregister(struct suspend_blocker *bl) {}
static inline void suspend_block(struct suspend_blocker *bl) {}
static inline void suspend_unblock(struct suspend_blocker *bl) {}
static inline bool suspend_blocker_is_active(struct suspend_blocker *bl)
{
	return false;
}
static inline bool suspend_is_blocked(void) { return false; }

static inline void suspend_blocking_work_init(
	struct suspend_blocking_work *work, work_func_t func, const char *name)
{
	INIT_WORK(&work->work, func);
}
static inline void suspend_blocking_work_destroy(
	struct suspend_blocking_work *work)
{
	cancel_work_sync(&work->work);
}
static inline int queue_suspend_blocking_work(
	struct workqueue_struct *wq, struct suspend_blocking_work *work)
{
	return queue_work(wq, &work->work);
}
static inline int schedule_suspend_blocking_work(
	struct suspend_blocking_work *work)
{
	return schedule_work(&work->work);
}
static inline int cancel_suspend_blocking_work_sync(
	struct suspend_blocking_work *work)
{
	return cancel_work_sync(&work->work);
}
#endif

#endif
