/*
 * arch/arm/mach-tegra/include/mach/gpio.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef __MACH_TEGRA_GPIO_H
#define __MACH_TEGRA_GPIO_H

#define ARCH_NR_GPIOS		224

#include <asm-generic/gpio.h>
#include <mach/gpio-names.h>

#define gpio_get_value		__gpio_get_value
#define gpio_set_value		__gpio_set_value
#define gpio_cansleep		__gpio_cansleep

static inline int gpio_to_irq(unsigned int gpio)
{
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned int irq)
{
	return -EINVAL;
}

void tegra_gpio_enable(int gpio);
void tegra_gpio_disable(int gpio);

#endif
