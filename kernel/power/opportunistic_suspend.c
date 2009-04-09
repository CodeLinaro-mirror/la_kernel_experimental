/*
 * kernel/power/opportunistic_suspend.c
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
#include <linux/debugfs.h>

#include "power.h"

extern struct workqueue_struct *pm_wq;

enum {
	DEBUG_EXIT_SUSPEND = 1U << 0,
	DEBUG_WAKEUP = 1U << 1,
	DEBUG_USER_STATE = 1U << 2,
	DEBUG_SUSPEND = 1U << 3,
	DEBUG_SUSPEND_BLOCKER = 1U << 4,
};
static int debug_mask = DEBUG_EXIT_SUSPEND | DEBUG_WAKEUP | DEBUG_USER_STATE;
module_param_named(debug_mask, debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int unknown_wakeup_delay_msecs = 500;
module_param_named(unknown_wakeup_delay_msecs, unknown_wakeup_delay_msecs, int,
		   S_IRUGO | S_IWUSR | S_IWGRP);

#define SB_INITIALIZED            (1U << 8)
#define SB_ACTIVE                 (1U << 9)

DEFINE_SUSPEND_BLOCKER(main_suspend_blocker, main);

static DEFINE_SPINLOCK(list_lock);
static DEFINE_SPINLOCK(state_lock);
static LIST_HEAD(inactive_blockers);
static LIST_HEAD(active_blockers);
static int current_event_num;
static suspend_state_t requested_suspend_state = PM_SUSPEND_MEM;
static bool enable_suspend_blockers;
static DEFINE_SUSPEND_BLOCKER(unknown_wakeup, unknown_wakeups);

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

static void print_active_suspend_blockers(void)
{
	struct suspend_blocker *blocker;

	list_for_each_entry(blocker, &active_blockers, link)
		pr_info("PM: Active suspend blocker %s\n", blocker->name);
}

/**
 * suspend_is_blocked - Check if there are active suspend blockers.
 *
 * Return true if suspend blockers are enabled and there are active suspend
 * blockers, in which case the system cannot be put to sleep opportunistically.
 */
bool suspend_is_blocked(void)
{
	return enable_suspend_blockers && !list_empty(&active_blockers);
}

static void expire_unknown_wakeup(unsigned long data)
{
	suspend_unblock(&unknown_wakeup);
}
static DEFINE_TIMER(expire_unknown_wakeup_timer, expire_unknown_wakeup, 0, 0);

static void suspend_worker(struct work_struct *work)
{
	int ret;
	int entry_event_num;

	enable_suspend_blockers = true;

	if (suspend_is_blocked()) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("PM: Automatic suspend aborted\n");
		goto abort;
	}

	entry_event_num = current_event_num;

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("PM: Automatic suspend\n");

	ret = pm_suspend(requested_suspend_state);

	if (debug_mask & DEBUG_EXIT_SUSPEND)
		pr_info_time("PM: Automatic suspend exit, ret = %d ", ret);

	if (current_event_num == entry_event_num) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("PM: pm_suspend() returned with no event\n");
		suspend_block(&unknown_wakeup);
		mod_timer(&expire_unknown_wakeup_timer,
			  msecs_to_jiffies(unknown_wakeup_delay_msecs));
	}

abort:
	enable_suspend_blockers = false;
}
static DECLARE_WORK(suspend_work, suspend_worker);

/**
 * suspend_blocker_register - Prepare a suspend blocker for being used.
 * @blocker: Suspend blocker to handle.
 *
 * The suspend blocker struct and name must not be freed before calling
 * suspend_blocker_unregister().
 */
void suspend_blocker_register(struct suspend_blocker *blocker)
{
	unsigned long irqflags = 0;

	WARN_ON(!blocker->name);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("%s: Registering %s\n", __func__, blocker->name);

	blocker->flags = SB_INITIALIZED;
	INIT_LIST_HEAD(&blocker->link);

	spin_lock_irqsave(&list_lock, irqflags);
	list_add(&blocker->link, &inactive_blockers);
	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_blocker_register);

/**
 * suspend_blocker_init - Initialize a suspend blocker's name and register it.
 * @blocker: Suspend blocker to initialize.
 * @name:    The name of the suspend blocker to show in debug messages and
 *	     /sys/kernel/debug/suspend_blockers.
 *
 * The suspend blocker struct and name must not be freed before calling
 * suspend_blocker_unregister().
 */
void suspend_blocker_init(struct suspend_blocker *blocker, const char *name)
{
	blocker->name = name;
	suspend_blocker_register(blocker);
}
EXPORT_SYMBOL(suspend_blocker_init);

/**
 * suspend_blocker_unregister - Unregister a suspend blocker.
 * @blocker: Suspend blocker to handle.
 */
void suspend_blocker_unregister(struct suspend_blocker *blocker)
{
	unsigned long irqflags;

	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;

	spin_lock_irqsave(&list_lock, irqflags);
	blocker->flags &= ~SB_INITIALIZED;
	list_del(&blocker->link);
	if ((blocker->flags & SB_ACTIVE) && list_empty(&active_blockers))
		queue_work(pm_wq, &suspend_work);
	spin_unlock_irqrestore(&list_lock, irqflags);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("%s: Unregistered %s\n", __func__, blocker->name);
}
EXPORT_SYMBOL(suspend_blocker_unregister);

/**
 * suspend_block - Block system suspend.
 * @blocker: Suspend blocker to use.
 *
 * It is safe to call this function from interrupt context.
 */
void suspend_block(struct suspend_blocker *blocker)
{
	unsigned long irqflags;

	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;

	spin_lock_irqsave(&list_lock, irqflags);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("%s: %s\n", __func__, blocker->name);

	blocker->flags |= SB_ACTIVE;
	list_move(&blocker->link, &active_blockers);

	current_event_num++;

	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_block);

/**
 * suspend_unblock - Allow system suspend to happen.
 * @blocker: Suspend blocker to unblock.
 *
 * If no other suspend blockers are active, schedule suspend of the system.
 *
 * It is safe to call this function from interrupt context.
 */
void suspend_unblock(struct suspend_blocker *blocker)
{
	unsigned long irqflags;

	if (WARN_ON(!(blocker->flags & SB_INITIALIZED)))
		return;

	spin_lock_irqsave(&list_lock, irqflags);

	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("%s: %s\n", __func__, blocker->name);

	list_move(&blocker->link, &inactive_blockers);
	if ((blocker->flags & SB_ACTIVE) && list_empty(&active_blockers))
		queue_work(pm_wq, &suspend_work);
	blocker->flags &= ~(SB_ACTIVE);

	if ((debug_mask & DEBUG_SUSPEND) && blocker == &main_suspend_blocker)
		print_active_suspend_blockers();

	spin_unlock_irqrestore(&list_lock, irqflags);
}
EXPORT_SYMBOL(suspend_unblock);

/**
 * suspend_blocker_is_active - Test if a suspend blocker is blocking suspend
 * @blocker: Suspend blocker to check.
 *
 * Returns true if the suspend_blocker is currently active.
 */
bool suspend_blocker_is_active(struct suspend_blocker *blocker)
{
	WARN_ON(!(blocker->flags & SB_INITIALIZED));

	return !!(blocker->flags & SB_ACTIVE);
}
EXPORT_SYMBOL(suspend_blocker_is_active);

bool opportunistic_suspend_valid_state(suspend_state_t state)
{
	return (state == PM_SUSPEND_ON) || valid_state(state);
}

int opportunistic_suspend_state(suspend_state_t state)
{
	unsigned long irqflags;

	if (!opportunistic_suspend_valid_state(state))
		return -ENODEV;

	spin_lock_irqsave(&state_lock, irqflags);

	if (debug_mask & DEBUG_USER_STATE)
		pr_info_time("%s: %s (%d->%d) at %lld ", __func__,
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

void __init opportunistic_suspend_init(void)
{
	suspend_blocker_register(&main_suspend_blocker);
	suspend_block(&main_suspend_blocker);
	suspend_blocker_register(&unknown_wakeup);
}

static struct dentry *suspend_blocker_stats_dentry;

static int suspend_blocker_stats_show(struct seq_file *m, void *unused)
{
	unsigned long irqflags;
	struct suspend_blocker *blocker;

	seq_puts(m, "name\tactive\n");
	spin_lock_irqsave(&list_lock, irqflags);
	list_for_each_entry(blocker, &inactive_blockers, link)
		seq_printf(m, "\"%s\"\t0\n", blocker->name);
	list_for_each_entry(blocker, &active_blockers, link)
		seq_printf(m, "\"%s\"\t1\n", blocker->name);
	spin_unlock_irqrestore(&list_lock, irqflags);
	return 0;
}

static int suspend_blocker_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, suspend_blocker_stats_show, NULL);
}

static const struct file_operations suspend_blocker_stats_fops = {
	.owner = THIS_MODULE,
	.open = suspend_blocker_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init suspend_blocker_debugfs_init(void)
{
	suspend_blocker_stats_dentry = debugfs_create_file("suspend_blockers",
			S_IRUGO, NULL, NULL, &suspend_blocker_stats_fops);
	return 0;
}

postcore_initcall(suspend_blocker_debugfs_init);
