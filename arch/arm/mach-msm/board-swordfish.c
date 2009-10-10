/* linux/arch/arm/mach-msm/board-swordfish.c
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/android_pmem.h>
#include <linux/msm_kgsl.h>
#include <linux/i2c.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/setup.h>

#include <mach/board.h>
#include <mach/irqs.h>
#include <mach/msm_iomap.h>
#include <mach/msm_hsusb.h>
#include <mach/msm_ts.h>

#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
#include <linux/usb/mass_storage_function.h>
#endif

#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android.h>
#endif

#include "board-swordfish.h"
#include "devices.h"
#include "proc_comm.h"

extern int swordfish_init_mmc(void);

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= 0x70000300,
		.end	= 0x70000400,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MSM_GPIO_TO_INT(156),
		.end	= MSM_GPIO_TO_INT(156),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

#ifdef CONFIG_USB_FUNCTION
static char *swordfish_usb_functions[] = {
#if defined(CONFIG_USB_FUNCTION_MASS_STORAGE)
	"usb_mass_storage",
#endif
#ifdef CONFIG_USB_FUNCTION_ADB
	"adb",
#endif
};

static struct msm_hsusb_product swordfish_usb_products[] = {
	{
		.product_id     = 0x0d01,
		.functions      = 0x00000001, /* "usb_mass_storage" only */
	},
	{
		.product_id     = 0x0d02,
		.functions      = 0x00000003, /* "usb_mass_storage" and "adb" */
	},
};
#endif

static int swordfish_phy_init_seq[] = { 0x1D, 0x0D, 0x1D, 0x10, -1 };

static struct msm_hsusb_platform_data msm_hsusb_pdata = {
	.phy_init_seq		= swordfish_phy_init_seq,
#ifdef CONFIG_USB_FUNCTION
	.vendor_id		= 0x18d1,
	.product_id		= 0x0d02,
	.version		= 0x0100,
	.product_name		= "Swordfish",
	.serial_number		= "42",
	.manufacturer_name	= "Qualcomm",

	.functions		= swordfish_usb_functions,
	.num_functions		= ARRAY_SIZE(swordfish_usb_functions),
	.products		= swordfish_usb_products,
	.num_products		= ARRAY_SIZE(swordfish_usb_products),
#endif
};

#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns		= 1,
	.buf_size	= 16384,
	.vendor		= "Qualcomm",
	.product	= "Swordfish",
	.release	= 0x0100,
};

static struct platform_device usb_mass_storage_device = {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &mass_storage_pdata,
	},
};
#endif

static struct resource msm_kgsl_resources[] = {
        {
                .name   = "kgsl_reg_memory",
                .start  = MSM_GPU_REG_PHYS,
                .end    = MSM_GPU_REG_PHYS + MSM_GPU_REG_SIZE - 1,
                .flags  = IORESOURCE_MEM,
        },
        {
                .name   = "kgsl_phys_memory",
                .start  = MSM_GPU_MEM_BASE,
                .end    = MSM_GPU_MEM_BASE + MSM_GPU_MEM_SIZE - 1,
                .flags  = IORESOURCE_MEM,
        },
        {
                .start  = INT_GRAPHICS,
                .end    = INT_GRAPHICS,
                .flags  = IORESOURCE_IRQ,
        },
};

static struct platform_device msm_kgsl_device = {
        .name           = "kgsl",
        .id             = -1,
        .resource       = msm_kgsl_resources,
        .num_resources  = ARRAY_SIZE(msm_kgsl_resources),
};

static struct android_pmem_platform_data mdp_pmem_pdata = {
        .name           = "pmem",
        .start          = MSM_PMEM_MDP_BASE,
        .size           = MSM_PMEM_MDP_SIZE,
        .no_allocator   = 0,
        .cached         = 1,
};

static struct android_pmem_platform_data android_pmem_gpu0_pdata = {
        .name           = "pmem_gpu0",
        .start          = MSM_PMEM_GPU0_BASE,
        .size           = MSM_PMEM_GPU0_SIZE,
        .no_allocator   = 0,
        .cached         = 0,
};

static struct android_pmem_platform_data android_pmem_gpu1_pdata = {
        .name           = "pmem_gpu1",
        .start          = MSM_PMEM_GPU1_BASE,
        .size           = MSM_PMEM_GPU1_SIZE,
        .no_allocator   = 0,
        .cached         = 0,
};

static struct android_pmem_platform_data android_pmem_adsp_pdata = {
        .name           = "pmem_adsp",
        .start          = MSM_PMEM_ADSP_BASE,
        .size           = MSM_PMEM_ADSP_SIZE,
        .no_allocator   = 0,
        .cached         = 0,
};

static struct platform_device android_pmem_mdp_device = {
        .name           = "android_pmem",
        .id             = 0,
        .dev            = {
                .platform_data = &mdp_pmem_pdata
        },
};

static struct platform_device android_pmem_adsp_device = {
        .name           = "android_pmem",
        .id             = 1,
        .dev            = {
                .platform_data = &android_pmem_adsp_pdata,
        },
};

static struct platform_device android_pmem_gpu0_device = {
        .name           = "android_pmem",
        .id             = 2,
        .dev            = {
                .platform_data = &android_pmem_gpu0_pdata,
        },
};

static struct platform_device android_pmem_gpu1_device = {
        .name           = "android_pmem",
        .id             = 3,
        .dev            = {
                .platform_data = &android_pmem_gpu1_pdata,
        },
};

#ifdef CONFIG_USB_ANDROID
static struct android_usb_platform_data android_usb_pdata = {
	.vendor_id		= 0x18d1,
	.product_id		= 0x0d01,
	.adb_product_id		= 0x0d02,
	.version		= 0x0100,
	.serial_number		= "42",
	.product_name		= "Swordfishdroid",
	.manufacturer_name	= "Qualcomm",
	.nluns			= 1,
};

static struct platform_device android_usb_device = {
	.name	= "android_usb",
	.id	= -1,
	.dev	= {
		.platform_data = &android_usb_pdata,
	},
};
#endif

static struct platform_device fish_battery_device = {
	.name = "fish_battery",
};

static struct msm_ts_platform_data swordfish_ts_pdata = {
	.min_x		= 296,
	.max_x		= 3800,
	.min_y		= 296,
	.max_y		= 3800,
	.min_press	= 0,
	.max_press	= 256,
	.inv_x		= 4096,
	.inv_y		= 4096,
};

extern struct sys_timer msm_timer;

static struct msm_acpu_clock_platform_data swordfish_clock_data = {
	.acpu_switch_time_us	= 20,
	.max_speed_delta_khz	= 256000,
	.vdd_switch_time_us	= 62,
	.power_collapse_khz	= 128000000,
	.wait_for_irq_khz	= 128000000,
};

void msm_serial_debug_init(unsigned int base, int irq,
			   struct device *clk_device, int signal_irq);

#ifdef CONFIG_MSM_CAMERA
static uint32_t camera_off_gpio_table[] = {
	/* parallel CAMERA interfaces */
	PCOM_GPIO_CFG(0,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT0 */
	PCOM_GPIO_CFG(1,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT1 */
	PCOM_GPIO_CFG(2,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT2 */
	PCOM_GPIO_CFG(3,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT3 */
	PCOM_GPIO_CFG(4,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT4 */
	PCOM_GPIO_CFG(5,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT5 */
	PCOM_GPIO_CFG(6,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT6 */
	PCOM_GPIO_CFG(7,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT7 */
	PCOM_GPIO_CFG(8,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT8 */
	PCOM_GPIO_CFG(9,  0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT9 */
	PCOM_GPIO_CFG(10, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT10 */
	PCOM_GPIO_CFG(11, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT11 */
	PCOM_GPIO_CFG(12, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* PCLK */
	PCOM_GPIO_CFG(13, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* HSYNC_IN */
	PCOM_GPIO_CFG(14, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* VSYNC_IN */
	PCOM_GPIO_CFG(15, 0, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_2MA), /* MCLK */
};

static uint32_t camera_on_gpio_table[] = {
	/* parallel CAMERA interfaces */
	PCOM_GPIO_CFG(0,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT0 */
	PCOM_GPIO_CFG(1,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT1 */
	PCOM_GPIO_CFG(2,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT2 */
	PCOM_GPIO_CFG(3,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT3 */
	PCOM_GPIO_CFG(4,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT4 */
	PCOM_GPIO_CFG(5,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT5 */
	PCOM_GPIO_CFG(6,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT6 */
	PCOM_GPIO_CFG(7,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT7 */
	PCOM_GPIO_CFG(8,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT8 */
	PCOM_GPIO_CFG(9,  1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT9 */
	PCOM_GPIO_CFG(10, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT10 */
	PCOM_GPIO_CFG(11, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* DAT11 */
	PCOM_GPIO_CFG(12, 0, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* PCLK */
	PCOM_GPIO_CFG(13, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* HSYNC_IN */
	PCOM_GPIO_CFG(14, 1, GPIO_INPUT, GPIO_PULL_DOWN, GPIO_2MA), /* VSYNC_IN */
	PCOM_GPIO_CFG(15, 1, GPIO_OUTPUT, GPIO_NO_PULL, GPIO_16MA), /* MCLK */
};

static void config_gpio_table(uint32_t *table, int len)
{
	int n;
	unsigned id;
	for (n = 0; n < len; n++) {
		id = table[n];
		msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &id, 0);
	}
}

static void config_camera_on_gpios(void)
{
	config_gpio_table(camera_on_gpio_table,
		ARRAY_SIZE(camera_on_gpio_table));
}

static void config_camera_off_gpios(void)
{
  config_gpio_table(camera_off_gpio_table,
		ARRAY_SIZE(camera_off_gpio_table));
}

static struct msm_camera_device_platform_data msm_camera_device_data = {
	.camera_gpio_on  = config_camera_on_gpios,
	.camera_gpio_off = config_camera_off_gpios,
	.ioext.mdcphy = MSM_MDC_PHYS,
	.ioext.mdcsz  = MSM_MDC_SIZE,
	.ioext.appphy = MSM_CLK_CTL_PHYS,
	.ioext.appsz  = MSM_CLK_CTL_SIZE,
};

static struct resource msm_camera_resources[] = {
	{
		.start  = MSM_VFE_PHYS,
		.end    = MSM_VFE_PHYS + MSM_VFE_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = INT_VFE,
		.end	= INT_VFE,
		.flags  = IORESOURCE_IRQ,
	},
};

#ifdef CONFIG_MT9D112
static struct msm_camera_sensor_info msm_camera_sensor_mt9d112_data = {
        .sensor_name    = "mt9d112",
        .sensor_reset   = 17,
        .sensor_pwd     = 85,
        .vcm_pwd        = 0,
        .pdata          = &msm_camera_device_data,
	.resource	= msm_camera_resources,
	.num_resources	= ARRAY_SIZE(msm_camera_resources),
};

static struct platform_device msm_camera_sensor_mt9d112 = {
        .name           = "msm_camera_mt9d112",
        .dev            = {
                .platform_data = &msm_camera_sensor_mt9d112_data,
        },
};
#endif

#ifdef CONFIG_MT9P012
static struct msm_camera_sensor_info msm_camera_sensor_mt9p012_data = {
        .sensor_name    = "mt9p012",
        .sensor_reset   = 17,
        .sensor_pwd     = 85,
        .vcm_pwd        = 0,
        .pdata          = &msm_camera_device_data,
	.resource	= msm_camera_resources,
	.num_resources	= ARRAY_SIZE(msm_camera_resources),
};

static struct platform_device msm_camera_sensor_mt9p012 = {
        .name           = "msm_camera_mt9p012",
        .dev            = {
                .platform_data = &msm_camera_sensor_mt9p012_data,
        },
};
#endif


#ifdef CONFIG_S5K3E2FX
static struct msm_camera_sensor_info msm_camera_sensor_s5k3e2fx_data = {
        .sensor_name    = "s5k3e2fx",
        .sensor_reset   = 17,
        .sensor_pwd     = 85,
        .vcm_pwd        = 0,
        .pdata          = &msm_camera_device_data,
	.resource	= msm_camera_resources,
	.num_resources	= ARRAY_SIZE(msm_camera_resources),
};

static struct platform_device msm_camera_sensor_s5k3e2fx = {
        .name           = "msm_camera_s5k3e2fx",
        .dev            = {
                .platform_data = &msm_camera_sensor_s5k3e2fx_data,
        },
};
#endif

#endif

static struct i2c_board_info i2c_devices[] = {
#ifdef CONFIG_MSM_CAMERA
#ifdef CONFIG_MT9P012
	{
		I2C_BOARD_INFO("mt9p012", 0x6C >> 1),
	},
#endif
#ifdef CONFIG_MT9D112
	{
		I2C_BOARD_INFO("mt9d112", 0x78 >> 1),
	},
#endif
#ifdef CONFIG_S5K3E2FX
	{
		I2C_BOARD_INFO("s5k3e2fx", 0x20 >> 1),
	},
#endif

#endif
};

static struct platform_device *devices[] __initdata = {
#if !defined(CONFIG_MSM_SERIAL_DEBUGGER)
	&msm_device_uart3,
#endif
	&msm_device_smd,
	&msm_device_nand,
	&msm_device_i2c,
	&msm_device_hsusb,
#ifdef CONFIG_USB_FUNCTION_MASS_STORAGE
	&usb_mass_storage_device,
#endif
#ifdef CONFIG_USB_ANDROID
	&android_usb_device,
#endif
	&fish_battery_device,
	&smc91x_device,
	&msm_device_touchscreen,
	&android_pmem_mdp_device,
	&android_pmem_adsp_device,
	&android_pmem_gpu0_device,
	&android_pmem_gpu1_device,
	&msm_kgsl_device,
#ifdef CONFIG_MT9D112
	&msm_camera_sensor_mt9d112,
#endif
#ifdef CONFIG_MT9P012
	&msm_camera_sensor_mt9p012,
#endif

#ifdef CONFIG_S5K3E2FX
	&msm_camera_sensor_s5k3e2fx,
#endif

};

static void __init swordfish_init(void)
{
	int rc;

	msm_acpu_clock_init(&swordfish_clock_data);
#if defined(CONFIG_MSM_SERIAL_DEBUGGER)
	msm_serial_debug_init(MSM_UART3_PHYS, INT_UART3,
			      &msm_device_uart3.dev, 1);
#endif
	msm_device_hsusb.dev.platform_data = &msm_hsusb_pdata;
	msm_device_touchscreen.dev.platform_data = &swordfish_ts_pdata;
	i2c_register_board_info(0, i2c_devices, ARRAY_SIZE(i2c_devices));
	platform_add_devices(devices, ARRAY_SIZE(devices));
	msm_hsusb_set_vbus_state(1);
	rc = swordfish_init_mmc();
	if (rc)
		pr_crit("%s: MMC init failure (%d)\n", __func__, rc);
}

static void __init swordfish_fixup(struct machine_desc *desc, struct tag *tags,
				 char **cmdline, struct meminfo *mi)
{
	mi->nr_banks = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].node = PHYS_TO_NID(PHYS_OFFSET);
	mi->bank[0].size = (101*1024*1024);
}

static void __init swordfish_map_io(void)
{
	msm_map_common_io();
	msm_clock_init();
}

MACHINE_START(SWORDFISH, "Swordfish Board (QCT SURF8250)")
#ifdef CONFIG_MSM_DEBUG_UART
	.phys_io        = MSM_DEBUG_UART_PHYS,
	.io_pg_offst    = ((MSM_DEBUG_UART_BASE) >> 18) & 0xfffc,
#endif
	.boot_params	= 0x20000100,
	.fixup		= swordfish_fixup,
	.map_io		= swordfish_map_io,
	.init_irq	= msm_init_irq,
	.init_machine	= swordfish_init,
	.timer		= &msm_timer,
MACHINE_END
