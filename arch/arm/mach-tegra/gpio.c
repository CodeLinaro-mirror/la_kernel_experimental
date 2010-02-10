/*
 * arch/arm/mach-tegra/gpio.c
 *
 * Copyright (c) 2010 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/init.h>

#include <asm/io.h>
#include <asm/gpio.h>

#include <mach/iomap.h>

#define GPIO_BANK(x)		((x) >> 5)
#define GPIO_PORT(x)		(((x) >> 3) & 0x3)
#define GPIO_BIT(x)		((x) & 0x7)

#define GPIO_REG(x)		(IO_TO_VIRT(TEGRA_GPIO_BASE) +	\
				 GPIO_BANK(x) * 0x80 +		\
				 GPIO_PORT(x) * 4)

#define GPIO_CNF(x)		(GPIO_REG(x) + 0x00)
#define GPIO_OE(x)		(GPIO_REG(x) + 0x10)
#define GPIO_OUT(x)		(GPIO_REG(x) + 0X20)
#define GPIO_IN(x)		(GPIO_REG(x) + 0x30)
#define GPIO_INT_STA(x)		(GPIO_REG(x) + 0x40)
#define GPIO_INT_ENB(x)		(GPIO_REG(x) + 0x50)
#define GPIO_INT_LVL(x)		(GPIO_REG(x) + 0x60)
#define GPIO_INT_CLR(x)		(GPIO_REG(x) + 0x70)

void tegra_gpio_enable(int gpio)
{
	__raw_writel(__raw_readl(GPIO_CNF(gpio)) | (1 << GPIO_BIT(gpio)),
		     GPIO_CNF(gpio));
}

void tegra_gpio_disable(int gpio)
{
	__raw_writel(__raw_readl(GPIO_CNF(gpio)) & ~(1 << GPIO_BIT(gpio)),
		     GPIO_CNF(gpio));
}

static void tegra_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (value) {
		__raw_writel(__raw_readl(GPIO_OUT(offset)) | (1 << GPIO_BIT(offset)),
			     GPIO_OUT(offset));
	} else {
		__raw_writel(__raw_readl(GPIO_OUT(offset)) & ~(1 << GPIO_BIT(offset)),
			     GPIO_OUT(offset));
	}
}

static int tegra_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	printk("KONK: gpio_get(%d)\n", offset);
	return (__raw_readl(GPIO_IN(offset)) >> GPIO_BIT(offset)) & 0x1;
}

static int tegra_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	__raw_writel(__raw_readl(GPIO_OE(offset)) & ~(1 << GPIO_BIT(offset)),
		     GPIO_OE(offset));
	return 0;
}

static int tegra_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
                                        int value)
{
	__raw_writel(__raw_readl(GPIO_OE(offset)) | (1 << GPIO_BIT(offset)),
		     GPIO_OE(offset));
	return 0;
}


static struct gpio_chip tegra_gpio_chip = {
	.label			= "tegra-gpio",
	.direction_input	= tegra_gpio_direction_input,
	.get			= tegra_gpio_get,
	.direction_output	= tegra_gpio_direction_output,
	.set			= tegra_gpio_set,
	.base			= 0,
	.ngpio			= ARCH_NR_GPIOS,
};

static int __init tegra_gpio_init(void)
{
	gpiochip_add(&tegra_gpio_chip);
	tegra_gpio_enable(0x45);
	return 0;
}

arch_initcall(tegra_gpio_init);
