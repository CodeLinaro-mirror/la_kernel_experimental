/* include/linux/suspend_blocker.h
 *
 * Copyright (C) 2007-2009 Google, Inc.
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

/**
 * struct suspend_blocker - the basic suspend_blocker structure
 * @link:	List entry for active or inactive list.
 * @flags:	Tracks initialized, active, stats and autoexpire state.
 * @name:	Name used for debugging.
 * @expires:	Time, in jiffies, to unblock suspend.
 * @count:	Number of times this blocker has been deacivated
 * @wakeup_count: Number of times this blocker was the first to block suspend
 *		after resume.
 * @total_time:	Total time this suspend blocker has prevented suspend.
 * @prevent_suspend_time: Time this suspend blocker has prevented suspend while
 *		user-space requested suspend.
 * @max_time:	Max time this suspend blocker has been continuously active
 * @last_time:	Monotonic clock when the active state last changed.
 *
 * When a suspend_blocker is active it prevents the system from entering
 * suspend.
 *
 * The suspend_blocker structure must be initialized by suspend_blocker_init()
 */

struct suspend_blocker {
#ifdef CONFIG_SUSPEND_BLOCKERS
	struct list_head    link;
	int                 flags;
	const char         *name;
	unsigned long       expires;
#ifdef CONFIG_SUSPEND_BLOCKER_STATS
	struct {
		int             count;
		int             expire_count;
		int             wakeup_count;
		ktime_t         total_time;
		ktime_t         prevent_suspend_time;
		ktime_t         max_time;
		ktime_t         last_time;
	} stat;
#endif
#endif
};

#ifdef CONFIG_SUSPEND_BLOCKERS

void suspend_blocker_init(struct suspend_blocker *blocker, const char *name);
void suspend_blocker_destroy(struct suspend_blocker *blocker);
void suspend_block(struct suspend_blocker *blocker);
void suspend_block_timeout(struct suspend_blocker *blocker, long timeout);
void suspend_unblock(struct suspend_blocker *blocker);
bool suspend_blocker_is_active(struct suspend_blocker *blocker);
bool suspend_is_blocked(void);

#else

static inline void suspend_blocker_init(struct suspend_blocker *blocker,
					const char *name) {}
static inline void suspend_blocker_destroy(struct suspend_blocker *blocker) {}
static inline void suspend_block(struct suspend_blocker *blocker) {}
static inline void suspend_block_timeout(struct suspend_blocker *bl, long t) {}
static inline void suspend_unblock(struct suspend_blocker *blocker) {}
static inline bool suspend_blocker_is_active(struct suspend_blocker *bl)
								{ return 0; }
static inline bool suspend_is_blocked(void) { return 0; }

#endif

#endif

