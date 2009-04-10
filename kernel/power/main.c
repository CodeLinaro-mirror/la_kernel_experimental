/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>

#include "power.h"

DEFINE_MUTEX(pm_mutex);

unsigned int pm_flags;
EXPORT_SYMBOL(pm_flags);

#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
struct pm_policy {
	const char *name;
	bool (*valid_state)(suspend_state_t state);
	int (*set_state)(suspend_state_t state);
};

static struct pm_policy policies[] = {
	{
		.name		= "forced",
		.valid_state	= valid_state,
		.set_state	= enter_state,
	},
	{
		.name		= "opportunistic",
		.valid_state	= opportunistic_suspend_valid_state,
		.set_state	= opportunistic_suspend_state,
	},
};

static int policy;

static inline bool hibernation_supported(void)
{
	return !strncmp(policies[policy].name, "forced", 6);
}

static inline bool pm_state_valid(int state_idx)
{
	return pm_states[state_idx] && policies[policy].valid_state(state_idx);
}

static inline int pm_enter_state(int state_idx)
{
	return policies[policy].set_state(state_idx);
}

#else

static inline bool hibernation_supported(void) { return true; }

static inline bool pm_state_valid(int state_idx)
{
	return pm_states[state_idx] && valid_state(state_idx);
}

static inline int pm_enter_state(int state_idx)
{
	return enter_state(state_idx);
}
#endif /* CONFIG_OPPORTUNISTIC_SUSPEND */

#ifdef CONFIG_PM_SLEEP

/* Routines for PM-transition notifications */

static BLOCKING_NOTIFIER_HEAD(pm_chain_head);

int register_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_pm_notifier);

int unregister_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_pm_notifier);

int pm_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(&pm_chain_head, val, NULL)
			== NOTIFY_BAD) ? -EINVAL : 0;
}

/* If set, devices may be suspended and resumed asynchronously. */
int pm_async_enabled = 1;

static ssize_t pm_async_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_async_enabled);
}

static ssize_t pm_async_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t n)
{
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	pm_async_enabled = val;
	return n;
}

power_attr(pm_async);

#ifdef CONFIG_PM_DEBUG
int pm_test_level = TEST_NONE;

static const char * const pm_tests[__TEST_AFTER_LAST] = {
	[TEST_NONE] = "none",
	[TEST_CORE] = "core",
	[TEST_CPUS] = "processors",
	[TEST_PLATFORM] = "platform",
	[TEST_DEVICES] = "devices",
	[TEST_FREEZER] = "freezer",
};

static ssize_t pm_test_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	char *s = buf;
	int level;

	for (level = TEST_FIRST; level <= TEST_MAX; level++)
		if (pm_tests[level]) {
			if (level == pm_test_level)
				s += sprintf(s, "[%s] ", pm_tests[level]);
			else
				s += sprintf(s, "%s ", pm_tests[level]);
		}

	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';

	return (s - buf);
}

static ssize_t pm_test_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	const char * const *s;
	int level;
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	mutex_lock(&pm_mutex);

	level = TEST_FIRST;
	for (s = &pm_tests[level]; level <= TEST_MAX; s++, level++)
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len)) {
			pm_test_level = level;
			error = 0;
			break;
		}

	mutex_unlock(&pm_mutex);

	return error ? error : n;
}

power_attr(pm_test);
#endif /* CONFIG_PM_DEBUG */

#endif /* CONFIG_PM_SLEEP */

struct kobject *power_kobj;

/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 *
 *	If policy is set to opportunistic, store() does not block until the
 *	system resumes, and it will try to re-enter the state until another
 *	state is requested. Suspend blockers are respected and the requested
 *	state will only be entered when no suspend blockers are active.
 *	Write "on" to disable.
 *
 *	If CONFIG_EARLYSUSPEND is set, early_suspend hooks are called when
 *	the requested state changes to or from "on"
 */
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	char *s = buf;
#ifdef CONFIG_SUSPEND
	int i;

	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (pm_state_valid(i))
			s += sprintf(s,"%s ", pm_states[i]);
	}
#endif
#ifdef CONFIG_HIBERNATION
	if (hibernation_supported())
		s += sprintf(s, "%s\n", "disk");
#else
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
#endif
	return (s - buf);
}

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
#ifdef CONFIG_SUSPEND
	suspend_state_t state = PM_SUSPEND_ON;
	const char * const *s;
#endif
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* First, check if we are requested to hibernate */
	if (len == 4 && !strncmp(buf, "disk", len)) {
		if (hibernation_supported())
			error = hibernate();
		goto Exit;
	}

#ifdef CONFIG_SUSPEND
	for (s = &pm_states[state]; state < PM_SUSPEND_MAX; s++, state++) {
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len))
			break;
	}
	if (state < PM_SUSPEND_MAX && *s)
		error = pm_enter_state(state);
#endif

 Exit:
	return error ? error : n;
}

power_attr(state);

#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
/**
 *	policy - set policy for state
 */
static ssize_t policy_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	char *s = buf;
	int i;

	for (i = 0; i < ARRAY_SIZE(policies); i++) {
		if (i == policy)
			s += sprintf(s, "[%s] ", policies[i].name);
		else
			s += sprintf(s, "%s ", policies[i].name);
	}
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
	return (s - buf);
}

static ssize_t policy_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t n)
{
	const char *s;
	char *p;
	int len;
	int i;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	for (i = 0; i < ARRAY_SIZE(policies); i++) {
		s = policies[i].name;
		if (s && len == strlen(s) && !strncmp(buf, s, len)) {
			mutex_lock(&pm_mutex);
			policies[policy].set_state(PM_SUSPEND_ON);
			policy = i;
			mutex_unlock(&pm_mutex);
			return n;
		}
	}
	return -EINVAL;
}

power_attr(policy);
#endif /* CONFIG_OPPORTUNISTIC_SUSPEND */

#ifdef CONFIG_PM_TRACE
int pm_trace_enabled;

static ssize_t pm_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_trace_enabled);
}

static ssize_t
pm_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
	       const char *buf, size_t n)
{
	int val;

	if (sscanf(buf, "%d", &val) == 1) {
		pm_trace_enabled = !!val;
		return n;
	}
	return -EINVAL;
}

power_attr(pm_trace);
#endif /* CONFIG_PM_TRACE */

static struct attribute * g[] = {
	&state_attr.attr,
#ifdef CONFIG_PM_TRACE
	&pm_trace_attr.attr,
#endif
#ifdef CONFIG_PM_SLEEP
	&pm_async_attr.attr,
#ifdef CONFIG_OPPORTUNISTIC_SUSPEND
	&policy_attr.attr,
#endif
#ifdef CONFIG_PM_DEBUG
	&pm_test_attr.attr,
#endif
#endif
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_OPPORTUNISTIC_SUSPEND)
struct workqueue_struct *pm_wq;
EXPORT_SYMBOL_GPL(pm_wq);

static int __init pm_start_workqueue(void)
{
	pm_wq = create_freezeable_workqueue("pm");

	return pm_wq ? 0 : -ENOMEM;
}
#else
static inline int pm_start_workqueue(void) { return 0; }
#endif

static int __init pm_init(void)
{
	int error = pm_start_workqueue();
	if (error)
		return error;
	opportunistic_suspend_init();
	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	return sysfs_create_group(power_kobj, &attr_group);
}

core_initcall(pm_init);
