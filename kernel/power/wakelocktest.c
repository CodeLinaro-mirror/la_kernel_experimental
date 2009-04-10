/* kernel/power/userwakelock.c
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

#include <linux/fs.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/pm_qos_params.h>
#include <linux/uaccess.h>
#include <linux/wakelock.h>

static DEFINE_MUTEX(ioctl_lock);
static struct wake_lock test_wake_lock;
static DEFINE_SPINLOCK(test_spin_lock);
static atomic_t test_atomic;
static atomic_t test_atomic2;

static void test_wake_lock1(int count, void *arg)
{
	while (count-- > 0)
		wake_lock(&test_wake_lock);
}

static void test_wake_unlock(int count, void *arg)
{
	while (count-- > 0)
		wake_unlock(&test_wake_lock);
}

static void test_wake_lock_unlock(int count, void *arg)
{
	while (count-- > 0) {
		wake_lock(&test_wake_lock);
		wake_unlock(&test_wake_lock);
	}
}

static void test_has_wake_lock(int count, void *arg)
{
	while (count-- > 0)
		has_wake_lock(WAKE_LOCK_IDLE);
}

static void test_atomic_inc(int count, void *arg)
{
	while (count-- > 0)
		atomic_inc(&test_atomic);
}

static void atomic_lock(void)
{
	if (atomic_cmpxchg(&test_atomic2, 0, 1) == 0)
		atomic_inc(&test_atomic);
}

static void atomic_unlock(void)
{
	if (atomic_cmpxchg(&test_atomic2, 1, 0) == 1)
		if (atomic_dec_return(&test_atomic) == -1)
			pr_info("atomic_unlock schedule sleep\n");
}

static void test_atomic_lock(int count, void *arg)
{
	while (count-- > 0)
		atomic_lock();
}

static void test_atomic_unlock(int count, void *arg)
{
	while (count-- > 0)
		atomic_unlock();
}

static void test_atomic_lock_unlock(int count, void *arg)
{
	while (count-- > 0) {
		atomic_lock();
		atomic_unlock();
	}
}

static void test_irq_save_restore(int count, void *arg)
{
	unsigned long flags;
	while (count-- > 0) {
		local_irq_save(flags);
		local_irq_restore(flags);
	}
}

static void test_preempt_disable_enable(int count, void *arg)
{
	while (count-- > 0) {
		preempt_disable();
		preempt_enable();
	}
}

static void test_spin_lock_unlock_irqsave(int count, void *arg)
{
	unsigned long irqflags;
	while (count-- > 0) {
		spin_lock_irqsave(&test_spin_lock, irqflags);
		spin_unlock_irqrestore(&test_spin_lock, irqflags);
	}
}

static void test_pm_qos_update_no_change(int count, void *arg)
{
	char *qos_name = arg;
	while (count-- > 0)
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY, qos_name, 0);
}

static void test_pm_qos_update_pair(int count, void *arg)
{
	char *qos_name = arg;
	while (count-- > 0) {
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY, qos_name, 1);
		pm_qos_update_requirement(PM_QOS_CPU_DMA_LATENCY, qos_name, 0);
	}
}


static struct {
	const char *name;
	void (*func)(int count, void *arg);
	void *arg;
} test_func[] = {
	{ "test_wake_lock", test_wake_lock1 },
	{ "test_wake_unlock", test_wake_unlock },
	{ "test_wake_lock_unlock", test_wake_lock_unlock },
	{ "test_has_wake_lock", test_has_wake_lock },
	{ "test_atomic_inc", test_atomic_inc },
	{ "test_atomic_lock", test_atomic_lock },
	{ "test_atomic_unlock", test_atomic_unlock },
	{ "test_atomic_lock_unlock", test_atomic_lock_unlock },
	{ "test_irq_save_restore", test_irq_save_restore },
	{ "test_preempt_disable_enable", test_preempt_disable_enable },
	{ "test_spin_lock_unlock_irqsave", test_spin_lock_unlock_irqsave },
	{ "test_pm_qos_update_no_change test", test_pm_qos_update_no_change, "test" },
	{ "test_pm_qos_update_pair test", test_pm_qos_update_pair, "test" },
	{ "test_pm_qos_update_no_change test0", test_pm_qos_update_no_change, "test0" },
	{ "test_pm_qos_update_pair test0", test_pm_qos_update_pair, "test0" },
	{ "test_pm_qos_update_no_change test9", test_pm_qos_update_no_change, "test9" },
	{ "test_pm_qos_update_pair test9", test_pm_qos_update_pair, "test9" },
};


static ssize_t test_wake_lock_write(
	struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	int i;
	int j;
	int count;
	ktime_t t1, t2, td;
	u64 ti;
	char string[256];

	if (len >= sizeof(string))
		len = sizeof(string) - 1;
	if(copy_from_user(string, buf, len))
		return -EFAULT;
	string[len] = '\0';
	count = simple_strtol(string, NULL, 0);

	for(j = 0; j < 2; j++) {
		if (j == 0)
			pr_info("running test with interrupts enabled\n");
		else if (j == 1) {
			pr_info("running test with interrupts disabled\n");
			local_irq_disable();
		}
		for (i = 0; i < ARRAY_SIZE(test_func); i++) {
			t1 = ktime_get();
			test_func[i].func(count, test_func[i].arg);
			t2 = ktime_get();
			td = ktime_sub(t2, t1);
			ti = ktime_divns(td, count);
			pr_info("%-35s: %7d iterations in %11lld ns, "
				"%6lld ns per iteration\n",
				test_func[i].name, count, ktime_to_ns(td), ti);
		}
		if (j == 1)
			local_irq_enable();
	}
	return len;
}

static const struct file_operations test_wakelock_fops = {
	.write = test_wake_lock_write
};

static struct miscdevice test_wakelock_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wakelocktest",
	.fops = &test_wakelock_fops,
};

static int __init test_wakelock_init(void)
{
	int i;
	char qos_name[] = "test?";
	wake_lock_init(&test_wake_lock, WAKE_LOCK_SUSPEND, "test-wake-lock");
	for (i = 0; i < 10; i++) {
		qos_name[4] = '0' + i;
		pm_qos_add_requirement(PM_QOS_CPU_DMA_LATENCY, qos_name, 0);
	}
	return misc_register(&test_wakelock_device);
}

static void __exit test_wakelock_exit(void)
{
	int i;
	char qos_name[] = "test?";
	misc_deregister(&test_wakelock_device);
	for (i = 0; i < 10; i++) {
		qos_name[4] = '0' + i;
		pm_qos_remove_requirement(PM_QOS_CPU_DMA_LATENCY, qos_name);
	}
	wake_lock_destroy(&test_wake_lock);
}

module_init(test_wakelock_init);
module_exit(test_wakelock_exit);
