/*
 * arch/arm/mach-tegra/include/mach/irqs.h
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

#ifndef __MACH_TEGRA_IRQS_H
#define __MACH_TEGRA_IRQS_H

/* Primary Interrupt Controller */
#define INT_TMR1			32
#define INT_TMR2			33
#define INT_RTC				34
#define INT_I2S2			35
#define INT_SHR_SEM_INBOX_IBF		36
#define INT_SHR_SEM_INBOX_IBE		37
#define INT_SHR_SEM_OUTBOX_IBF		38
#define INT_SHR_SEM_OUTBOX_IBE		39
#define INT_VDE_UCQ_ERROR		40
#define INT_VDE_SYNC_TOKEN		41
#define INT_VDE_BSE_V			42
#define INT_VDE_BSE_A			43
#define INT_VDE_SXE			44
#define INT_I2S1			45
#define INT_SDIO1			46
#define INT_SDIO2			47
#define INT_VDE				49
#define INT_USB				52
#define INT_USB2			53
#define INT_HSMMC			54
#define INT_EIDE			55
#define INT_NANDFLASH			56
#define INT_VCP				57
#define INT_APB_DMA			58
#define INT_AHB_DMA			59
#define INT_GNT_0			60
#define INT_GNT_1			61
#define INT_SECONDARY_NIRQ		62
#define INT_SECONDARY_NFIQ		63

/* Secondary Interrupt Controller */
#define INT_GPIO1			64
#define INT_GPIO2			65
#define INT_GPIO3			66
#define INT_GPIO4			67
#define INT_UARTA			68
#define INT_UARTB			69
#define INT_I2C				60
#define INT_SPI				71
#define INT_TWC				72
#define INT_TMR3			73
#define INT_TMR4			74
#define INT_FLOW_RSM0			75
#define INT_FLOW_RSM1			76
#define INT_SPDIF			77
#define INT_UARTC			78
#define INT_MIPI			79
#define INT_EVENTA			80
#define INT_EVENTB			81
#define INT_EVENTC			82
#define INT_EVENTD			83
#define INT_VFIR			84
#define INT_DVC				85
#define INT_SYS_STATS_MON		86
#define INT_GPIO5			87
#define INT_CPU_INTR			88
#define INT_SPI1			91
#define INT_SPB_DMA_COP			92
#define INT_AHB_DMA_COP			93
#define INT_DMA_TX			94
#define INT_DMA_RX			95

/* Tertiary Interrupt Controller */
#define INT_HOST1X_COP_SYNCPT		96
#define INT_HOST1X_MPCORE_SYNCPT	97
#define INT_HOST1X_COP_GENERAL		98
#define INT_HOST1X_MPCORE_GENERAL	99
#define INT_MPE_GENERAL			100
#define INT_VI_GENERAL			101
#define INT_EPP_GENERAL			102
#define INT_ISP_GENERAL			103
#define INT_2D_GENERAL			104
#define INT_DISPLAY_GENERAL		105
#define INT_DISPLAY_B_GENERAL		106
#define INT_HDMI			107
#define INT_TVO_GENERAL			108
#define INT_MC_GENERAL			109
#define INT_EMC_GENERAL			110
#define INT_CMC_GENERAL			111
#define INT_NOR_FLASH			112
#define INT_AC97			113
#define INT_SPI_2			114
#define INT_SPI_3			115
#define INT_I2C2			116
#define INT_KBC				117
#define INT_EXTERNAL_PMU		118
#define INT_GPIO6			119
#define INT_TVDAC			120
#define INT_GPIO7                       121
#define INT_UARTD                       122
#define INT_UARTE                       123
#define INT_I2C3                        124
#define INT_SPI4                        125
#define INT_SW_RESERVED			127

#define NR_IRQS 160
#endif
