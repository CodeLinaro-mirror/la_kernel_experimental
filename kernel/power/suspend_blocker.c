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
#include <linux/debugfs.h>
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
#define SB_PREVENTING_SUSPEND     (1U << 10)

static DEFINE_SPINLOCK(list_lock);
static DEFINE_SPINLOCK(state_lock);
static LIST_HEAD(inactive_blockers);
static LIST_HEAD(active_blockers);
static int current_event_num;
struct workqueue_struct *suspend_work_queue;
struct suspend_blocker main_suspend_blocker;
static suspend_state_t requested_suspend_state = PM_SUSPEND_MEM;
static bool enable_suspend_blockers;
static struct suspend_blocker unknown_wakeup;
static struct dentry *suspend_blocker_stats_dentry;

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

#ifdef CONFIG_SUSPEND_BLOCKER_STATS
static struct suspend_blocker deleted_suspend_blockers;
static ktime_t last_sleep_time_update;
static bool wait_for_wakeup;

static int print_blocker_stat(struct seq_file *m,
			      struct suspend_blocker *blocker)
{
	int lock_count = blocker->stat.count;
	ktime_t active_time = ktime_set(0, 0);
	ktime_t total_time = blocker->stat.total_time;
	ktime_t max_time = blocker->stat.max_time;
	ktime_t prevent_suspend_time = blocker->stat.prevent_suspend_time;
	if (blocker->flags & SB_ACTIVE) {
		ktime_t now, add_time;
		now = ktime_get();
		add_time = ktime_sub(now, blocker->stat.last_time);
		lock_count++;
		active_time = add_time;
		total_time = ktime_add(total_time, add_time);
		if (blocker->flags & SB_PREVENTING_SUSPEND)
			prevent_suspend_time = ktime_add(prevent_suspend_time,
					ktime_sub(now, last_sleep_time_update));
		if (add_time.tv64 > max_time.tv64)
			max_time = add_time;
	}

	return seq_printf(m, "\"%s\"\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\n",
		       blocker->name, lock_count, blocker->stat.wakeup_count,
		       ktime_to_ns(active_time), ktime_to_ns(total_time),
		       ktime_to_ns(prevent_suspend_time), ktime_to_ns(max_time),
		       ktime_to_ns(blocker->stat.last_time));
}


static int suspend_blocker_stats_show(struct seq_file *m, void *unused)
{
	unsigned long irqflags;
	struct suspend_blocker *blocker;

	seq_puts(m, "name\tcount\twake_count\tactive_since"
		 "\ttotal_time\tsleep_time\tmax_time\tlast_change\n");
	spin_lock_irqsave(&list_lock, irqflags);
	list_for_each_entry(blocker, &inactive_blockers, link)
		print_blocker_stat(m, blocker);
	list_for_each_entry(blocker, &active_blockers, link)
		print_blocker_stat(m, blocker);
	spin_unlock_irqrestore(&list_lock, irqflags);
	return 0;
}

static void suspend_blocker_stat_init_locked(struct suspend_blocker *blocker)
{
	blocker->stat.count = 0;
	blocker->stat.wakeup_count = 0;
	blocker->stat.total_time = ktime_set(0, 0);
	blocker->stat.prevent_suspend_time = ktime_set(0, 0);
	blocker->stat.max_time = ktime_set(0, 0);
	blocker->stat.last_time = ktime_set(0, 0);
}

static void suspend_blocker_stat_destroy_locked(struct suspend_blocker *bl)
{
	if (!bl->stat.count)
		return;
	deleted_suspend_blockers.stat.count += bl->stat.count;
	deleted_suspend_blockers.stat.total_time = ktime_add(
		deleted_suspend_blockers.stat.total_time, bl->stat.total_time);
	deleted_suspend_blockers.stat.prevent_suspend_time = ktime_add(
		deleted_suspend_blockers.stat.prevent_suspend_time,
		bl->stat.prevent_suspend_time);
	deleted_suspend_blockers.stat.max_time = ktime_add(
		deleted_suspend_blockers.stat.max_time, bl->stat.max_time);
}

static void suspend_unblock_stat_locked(struct suspend_blocker *blocker)
{
	ktime_t duration;
	ktime_t now;
	if (!(blocker->flags & SB_ACTIVE))
		return;
	now = ktime_get();
	blocker->stat.count++;
	duration = ktime_sub(now, blocker->stat.last_time);
	blocker->stat.total_time =
		ktime_add(blocker->stat.total_time, duration);
	if (ktime_to_ns(duration) > ktime_to_ns(blocker->stat.max_time))
		blocker->stat.max_time = duration;
	blocker->stat.last_time = ktime_get();
	if (blocker->flags & SB_PREVENTING_SUSPEND) {
		duration = ktime_sub(now, last_sleep_time_update);
		blocker->stat.prevent_suspend_time = ktime_add(
			blocker->stat.prevent_suspend_time, duration);
		blocker->flags &= ~SB_PREVENTING_SUSPEND;
	}
}

static void suspend_block_stat_locked(struct suspend_blocker *blocker)
{
	if (wait_for_wakeup) {
		if (debug_mask & DEBUG_WAKEUP)
			pr_info("wakeup suspend blocker: %s\n", blocker->name);
		wait_for_wakeup = false;
		blocker->stat.wakeup_count++;
	}
	if (!(blocker->flags & SB_ACTIVE))
		blocker->stat.last_time = ktime_get();
}

static void update_sleep_wait_stats_locked(bool done)
{
	struct suspend_blocker *blocker;
	ktime_t now, elapsed, add;

	now = ktime_get();
	elapsed = ktime_sub(now, last_sleep_time_update);
	list_for_each_entry(blocker, &active_blockers, link) {
		if (blocker->flags & SB_PREVENTING_SUSPEND) {
			add = elapsed;
			blocker->stat.prevent_suspend_time = ktime_add(
				blocker->stat.prevent_suspend_time, add);
		}
		if (done)
			blocker->flags &= ~SB_PREVENTING_SUSPEND;
		else
			blocker->flags |= SB_PREVENTING_SUSPEND;
	}
	last_sleep_time_update = now;
}

void about_to_enter_suspend(void)
{
	wait_for_wakeup = true;
}

#else

static inline void suspend_blocker_stat_init_locked(
					struct suspend_blocker *blocker) {}
static inline void suspend_blocker_stat_destroy_locked(
					struct suspend_blocker *blocker) {}
static inline void suspend_block_stat_locked(
					struct suspend_blocker *blocker) {}
static inline void suspend_unblock_stat_locked(
					struct suspend_blocker *blocker) {}
static inline void update_sleep_wait_stats_locked(bool done) {}

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

#endif

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

	if (suspend_is_blocked()) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("suspend: abort suspend\n");
		goto abort;
	}

	entry_event_num = current_event_num;
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("suspend: enter suspend\n");
	ret = pm_suspend(requested_suspend_state);
	if (debug_mask & DEBUG_EXIT_SUSPEND)
		pr_info_time("suspend: exit suspend, ret = %d ", ret);
	if (current_event_num == entry_event_num) {
		if (debug_mask & DEBUG_SUSPEND)
			pr_info("suspend: pm_suspend returned with no event\n");
		suspend_block(&unknown_wakeup);
		suspend_unblock(&unknown_wakeup);
	}
abort:
	enable_suspend_blockers = false;
}
static DECLARE_WORK(suspend_work, suspend_worker);

/**
 * suspend_blocker_init() - Initialize a suspend blocker
 * @blocker:	The suspend blocker to initialize.
 * @name:	The name of the suspend blocker to show in debug messages and
 *		/sys/kernel/debug/suspend_blockers.
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
	suspend_blocker_stat_init_locked(blocker);
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
	suspend_blocker_stat_destroy_locked(blocker);
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
	suspend_block_stat_locked(blocker);
	blocker->flags |= SB_ACTIVE;
	list_del(&blocker->link);
	if (debug_mask & DEBUG_SUSPEND_BLOCKER)
		pr_info("suspend_block: %s\n", blocker->name);
	list_add(&blocker->link, &active_blockers);

	current_event_num++;
	if (blocker == &main_suspend_blocker)
		update_sleep_wait_stats_locked(true);
	else if (!suspend_blocker_is_active(&main_suspend_blocker))
		update_sleep_wait_stats_locked(false);
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

	suspend_unblock_stat_locked(blocker);

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
		update_sleep_wait_stats_locked(false);
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

static int __init suspend_block_init(void)
{
	suspend_work_queue = create_singlethread_workqueue("suspend");
	if (!suspend_work_queue)
		return -ENOMEM;

	suspend_blocker_init(&main_suspend_blocker, "main");
	suspend_block(&main_suspend_blocker);
	suspend_blocker_init(&unknown_wakeup, "unknown_wakeups");
#ifdef CONFIG_SUSPEND_BLOCKER_STATS
	suspend_blocker_init(&deleted_suspend_blockers,
				"deleted_suspend_blockers");
#endif
	return 0;
}

static int __init suspend_block_postcore_init(void)
{
	if (!suspend_work_queue)
		return 0;
	suspend_blocker_stats_dentry = debugfs_create_file("suspend_blockers",
			S_IRUGO, NULL, NULL, &suspend_blocker_stats_fops);
	return 0;
}

core_initcall(suspend_block_init);
postcore_initcall(suspend_block_postcore_init);
