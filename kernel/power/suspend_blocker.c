/* kernel/power/suspend_blocker.c
 *
 * Copyright (C) 2005-2010 Google, Inc.
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

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/suspend.h>
#include <linux/suspend_blocker.h>
#include "power.h"

enum {
	DEBUG_EXIT_SUSPEND = 1U << 0,
	DEBUG_WAKEUP = 1U << 1,
	DEBUG_USER_STATE = 1U << 2,
	DEBUG_SUSPEND = 1U << 3,
	DEBUG_SUSPEND_BLOCKER = 1U << 4,
};
static int debug_mask = DEBUG_EXIT_SUSPEND | DEBUG_WAKEUP | DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define SB_INITIALIZED            (1U << 8)
#define SB_ACTIVE                 (1U << 9)

static DEFINE_SPINLOCK(list_lock);
static DEFINE_SPINLOCK(state_lock);
static LIST_HEAD(inactive_blockers);
static LIST_HEAD(active_blockers);
static int current_event_num;
struct workqueue_struct *suspend_work_queue;
struct suspend_blocker main_suspend_blocker;
static suspend_state_t requested_suspend_state = PM_SUSPEND_MEM;
static bool enable_suspend_blockers;

#define pr_info_time(fmt, args...) \
	do { \
		struct timespec ts; \
		struct rtc_time tm; \
		getnstimeofday(&ts); \
		rtc_time_to_tm(ts.tv_sec, &tm); \
		pr_info(fmt "(%d-%02d-%02d %02d:%02d:%02d.%09lu UTC)\n" , \
			args, \
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec); \
	} while (0);

static void print_active_blockers_locked(void)
{
	struct suspend_blocker *blocker;

	list_for_each_entry(blocker, &active_blockers, link)
		pr_info("active suspend blocker %s\n", blocker->name);
}

/**
 * suspend_is_blocked() - Check if suspend should be blocked
 *
 * suspend_is_blocked can be used by generic power management code to abort
 * suspend.
 *
 * To preserve backward compatibility suspend_is_blocked returns 0 unless it
 * is called during suspend initiated from the suspend_block code.
 */
bool suspend_is_blocked(void)
{
	if (!enable_suspend_blockers)
		return 0;
	return !list_empty(&active_blockers);
}

static void suspend_worker(struct work_struct *work)
{
	int ret;
	int entry_event_num;

	enable_suspend_blockers = true;
	while (!suspend_is_blocked()) {
		entry_event_num = current_event_num;
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("suspend: enter suspend\n");
		ret = pm_suspend(requested_suspend_state);
		if (debug_mask & DEBUG_EXIT_SUSPEND)
			pr_info_time("suspend: exit suspend, ret = %d ", ret);
		if (current_event_num == entry_event_num)
			pr_info("suspend: pm_suspend returned with no event\n");
	}
	enable_suspend_blockers = false;
}
static DECLARE_WORK(suspend_work, suspend_worker);

/**
 * suspend_blocker_init() - Initialize a suspend blocker
 * @blocker:	The suspend blocker to initialize.
 * @name:	The name of the suspend blocker to show in debug messages.
 *
 * The suspend blocker struct and name must not be freed before calling
 * suspend_blocker_destroy.
 */
void suspend_blocker_init(struct suspend_blocker *blocker, const char *name)
{
	unsigned long irqflags = 0;

	WARN_ON(!name);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("suspend_blocker_init name=%s\n", name);

	blocker->name = name;
	blocker->flags = SB_INITIALIZED;
	INIT_LIST_HEAD(&blocker->link);

	spin_lock_irqsave(&list_lock, irqflags);
	list_add(&blocker->link, &inactive_blockers);
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_blocker_init);

/**
 * suspend_blocker_destroy() - Destroy a suspend blocker
 * @blocker:	The suspend blocker to destroy.
 */
void suspend_blocker_destroy(struct suspend_blocker *blocker)
{
	unsigned long irqflags;
	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;
	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("suspend_blocker_destroy name=%s\n", blocker->name);
	spin_lock_irqsave(&list_lock, irqflags);
	blocker->flags &= ~SB_INITIALIZED;
	list_del(&blocker->link);
	if ((blocker->flags & SB_ACTIVE) && list_empty(&active_blockers))
		queue_work(suspend_work_queue, &suspend_work);
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_blocker_destroy);

/**
 * suspend_block() - Block suspend
 * @blocker:	The suspend blocker to use
 */
void suspend_block(struct suspend_blocker *blocker)
{
	unsigned long irqflags;

	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;

	spin_lock_irqsave(&list_lock, irqflags);
	blocker->flags |= SB_ACTIVE;
	list_del(&blocker->link);
	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("suspend_block: %s\n", blocker->name);
	list_add(&blocker->link, &active_blockers);

	current_event_num++;
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_block);

/**
 * suspend_unblock() - Unblock suspend
 * @blocker:	The suspend blocker to unblock.
 *
 * If no other suspend blockers block suspend, the system will suspend.
 */
void suspend_unblock(struct suspend_blocker *blocker)
{
	unsigned long irqflags;

	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;

	spin_lock_irqsave(&list_lock, irqflags);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("suspend_unblock: %s\n", blocker->name);
	list_del(&blocker->link);
	list_add(&blocker->link, &inactive_blockers);

	if ((blocker->flags & SB_ACTIVE) && list_empty(&active_blockers))
		queue_work(suspend_work_queue, &suspend_work);
	blocker->flags &= ~(SB_ACTIVE);
	if (blocker == &main_suspend_blocker) {
		if (debug_mask & DEBUG_SUSPEND)
			print_active_blockers_locked();
	}
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_unblock);

/**
 * suspend_blocker_is_active() - Test if a suspend blocker is blocking suspend
 * @blocker:	The suspend blocker to check.
 *
 * Returns true if the suspend_blocker is currently active.
 */
bool suspend_blocker_is_active(struct suspend_blocker *blocker)
{
	WARN_ON(!(blocker->flags & SB_INITIALIZED));

	return !!(blocker->flags & SB_ACTIVE);
}
EXPORT_SYMBOL(suspend_blocker_is_active);

bool request_suspend_valid_state(suspend_state_t state)
{
	return (state == PM_SUSPEND_ON) || valid_state(state);
}

int request_suspend_state(suspend_state_t state)
{
	unsigned long irqflags;

	if (!request_suspend_valid_state(state))
		return -ENODEV;
	
	spin_lock_irqsave(&state_lock, irqflags);
	if (debug_mask & DEBUG_USER_STATE)
		pr_info_time("request_suspend_state: %s (%d->%d) at %lld ",
			     state != PM_SUSPEND_ON ? "sleep" : "wakeup",
			     requested_suspend_state, state,
			     ktime_to_ns(ktime_get()));
	requested_suspend_state = state;
	if (state == PM_SUSPEND_ON)
		suspend_block(&main_suspend_blocker);
	else
		suspend_unblock(&main_suspend_blocker);
	spin_unlock_irqrestore(&state_lock, irqflags);
	return 0;
}

static int __init suspend_block_init(void)
{
	suspend_work_queue = create_singlethread_workqueue("suspend");
	if (!suspend_work_queue)
		return -ENOMEM;

	suspend_blocker_init(&main_suspend_blocker, "main");
	suspend_block(&main_suspend_blocker);
	return 0;
}

core_initcall(suspend_block_init);
