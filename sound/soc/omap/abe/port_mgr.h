/*
 * ABE Port manager
 *
 * Author:		Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_SOC_OMAP_PORT_MGR_H
#define __LINUX_SND_SOC_OMAP_PORT_MGR_H

/*
 * TODO: These structures, enums and port ID macros should be moved to the
 * new public ABE API header.
 */

/* Logical PORT IDs - Backend */
#define OMAP_ABE_BE_PORT_DMIC0			0
#define OMAP_ABE_BE_PORT_DMIC1			1
#define OMAP_ABE_BE_PORT_DMIC2			2
#define OMAP_ABE_BE_PORT_PDM_DL1		3
#define OMAP_ABE_BE_PORT_PDM_DL2		4
#define OMAP_ABE_BE_PORT_PDM_VIB		5
#define OMAP_ABE_BE_PORT_PDM_UL1		6
#define OMAP_ABE_BE_PORT_BT_VX_DL		7
#define OMAP_ABE_BE_PORT_BT_VX_UL		8
#define OMAP_ABE_BE_PORT_MM_EXT_UL		9
#define OMAP_ABE_BE_PORT_MM_EXT_DL		10

/* Logical PORT IDs - Frontend */
#define OMAP_ABE_FE_PORT_MM_DL1		11
#define OMAP_ABE_FE_PORT_MM_UL1		12
#define OMAP_ABE_FE_PORT_MM_UL2		13
#define OMAP_ABE_FE_PORT_VX_DL		14
#define OMAP_ABE_FE_PORT_VX_UL		15
#define OMAP_ABE_FE_PORT_VIB		16
#define OMAP_ABE_FE_PORT_TONES		17

#define OMAP_ABE_MAX_PORT_ID	(OMAP_ABE_FE_PORT_TONES + 1)

/* ports can either be enabled or disabled */
enum port_state {
	PORT_DISABLED = 0,
	PORT_ENABLED,
};

/* structure used for client port info */
struct omap_abe_port {

	/* logical and physical port IDs that correspond this port */
	int logical_id;
	int physical_id;

	/* enabled or disabled */
	enum port_state state;

	struct list_head list;
};

/* main ABE structure */
struct abe {

	/* List of open ABE logical ports */
	struct list_head ports;

	/* spinlock */
	spinlock_t lock;
};

struct omap_abe_port *omap_abe_port_open(struct abe *abe, int logical_id);
void omap_abe_port_close(struct abe *abe, struct omap_abe_port *port);
int omap_abe_port_enable(struct abe *abe, struct omap_abe_port *port);
int omap_abe_port_disable(struct abe *abe, struct omap_abe_port *port);
int omap_abe_port_is_enabled(struct abe *abe, struct omap_abe_port *port);

#endif /* __LINUX_SND_SOC_OMAP_PORT_MGR_H */
