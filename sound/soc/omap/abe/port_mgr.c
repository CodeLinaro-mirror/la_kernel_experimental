/*
 * ALSA SoC OMAP ABE port manager
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include "port_mgr.h"
#include "abe_typ.h"
#include "abe_api.h"


/*
 * Get the Physical port ID based on the logical port ID
 *
 * FE and BE ports have unique ID's within the driver but share
 * ID's within the ABE. This maps a driver port ID to an ABE port ID.
 */
static int get_physical_id(int logical_id)
{
	switch (logical_id) {
	/* backend ports */
	case OMAP_ABE_BE_PORT_DMIC0:
	case OMAP_ABE_BE_PORT_DMIC1:
	case OMAP_ABE_BE_PORT_DMIC2:
		return DMIC_PORT;
	case OMAP_ABE_BE_PORT_PDM_DL1:
	case OMAP_ABE_BE_PORT_PDM_DL2:
		return PDM_DL_PORT;
	case OMAP_ABE_BE_PORT_PDM_VIB:
		return VIB_DL_PORT;
	case OMAP_ABE_BE_PORT_PDM_UL1:
		return PDM_UL_PORT;
	case OMAP_ABE_BE_PORT_BT_VX_DL:
		return BT_VX_DL_PORT;
	case OMAP_ABE_BE_PORT_BT_VX_UL:
		return BT_VX_UL_PORT;
	case OMAP_ABE_BE_PORT_MM_EXT_UL:
		return MM_EXT_OUT_PORT;
	case OMAP_ABE_BE_PORT_MM_EXT_DL:
		return MM_EXT_IN_PORT;
	/* front end ports */
	case OMAP_ABE_FE_PORT_MM_DL1:
		return MM_DL_PORT;
	case OMAP_ABE_FE_PORT_MM_UL1:
		return MM_UL_PORT;
	case OMAP_ABE_FE_PORT_MM_UL2:
		return MM_UL2_PORT;
	case OMAP_ABE_FE_PORT_VX_DL:
		return MM_DL_PORT;
	case OMAP_ABE_FE_PORT_VX_UL:
		return VX_UL_PORT;
	case OMAP_ABE_FE_PORT_VIB:
		return VIB_DL_PORT;
	case OMAP_ABE_FE_PORT_TONES:
		return TONES_DL_PORT;
	}
	return -EINVAL;
}

/*
 * Get the number of enabled users of the physical port shared by this client.
 * Locks held by callers.
 */
static int port_get_num_users(struct abe *abe, struct omap_abe_port *port)
{
	struct omap_abe_port *p;
	int users = 0;

	list_for_each_entry(p, &abe->ports, list) {
		if (p->physical_id == port->physical_id && p->state == PORT_ENABLED)
			users++;
	}
	return users;
}

static int port_is_phy_enabled(struct abe *abe, int phy_port)
{
	struct omap_abe_port *p;

	list_for_each_entry(p, &abe->ports, list) {
		if (p->physical_id == phy_port && p->state == PORT_ENABLED)
			return 1;
	}
	return 0;
}

/*
 * Check whether the physical port is enabled for this PHY port ID.
 * Locks held by callers.
 */
int omap_abe_port_is_enabled(struct abe *abe, struct omap_abe_port *port)
{
	struct omap_abe_port *p;

	list_for_each_entry(p, &abe->ports, list) {
		if (p->physical_id == port->physical_id && p->state == PORT_ENABLED)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(omap_abe_port_is_enabled);

static void port_select_main(struct abe *abe, struct omap_abe_port *port)
{
	/* TODO - implement port selection logic here */
	if (port_is_phy_enabled(abe, PDM_DL_PORT)) {
		// TODO select PDM_DL as main port
		abe_select_main_port(PDM_DL_PORT);
	} else if (port_is_phy_enabled(abe, PDM_UL_PORT)) {
		// TODO select PDM_UL as main port
	} // TODO etc etc
}

/*
 * omap_abe_port_enable - enable ABE logical port
 *
 * @abe -  ABE.
 * @port - logical ABE port ID to be enabled.
 */
int omap_abe_port_enable(struct abe *abe, struct omap_abe_port *port)
{
	int ret = 0;

	spin_lock(&abe->lock);

	/* only enable the physical port iff it is disabled */ 
	if (port_get_num_users(abe, port) > 0)
		goto out;

	/* enable the physical port */
	abe_enable_data_transfer(port->logical_id);
	port_select_main(abe, port);

out:
	port->state = PORT_ENABLED;
	spin_unlock(&abe->lock);
	return ret;
}
EXPORT_SYMBOL(omap_abe_port_enable);

/*
 * omap_abe_port_disable - disable ABE logical port
 *
 * @abe -  ABE.
 * @port - logical ABE port ID to be disabled.
 */
int omap_abe_port_disable(struct abe *abe, struct omap_abe_port *port)
{
	int ret = 0;

	spin_lock(&abe->lock);

	/* only disable the port iff no other users are using it */
	if (port_get_num_users(abe, port) != 1)
		goto out;
 
	/* disable the physical port */
	abe_disable_data_transfer(port->logical_id);
	port_select_main(abe, port);
	
out:
	port->state = PORT_DISABLED;
	spin_unlock(&abe->lock);
	return ret;
}
EXPORT_SYMBOL(omap_abe_port_disable);

/*
 * omap_abe_port_open - open ABE logical port
 *
 * @abe -  ABE.
 * @logical_id - logical ABE port ID to be opened.
 */
struct omap_abe_port *omap_abe_port_open(struct abe *abe, int logical_id)
{
	struct omap_abe_port *port;

	port = kzalloc(sizeof(struct omap_abe_port), GFP_KERNEL);
	if (port == NULL)
		return NULL;

	port->logical_id = logical_id;
	port->physical_id = get_physical_id(logical_id);
	port->state = PORT_DISABLED;

	spin_lock(&abe->lock);
	list_add(&port->list, &abe->ports);
	spin_unlock(&abe->lock);

	return port;
}
EXPORT_SYMBOL(omap_abe_port_open);

/*
 * omap_abe_port_close - close ABE logical port
 *
 * @port - logical ABE port to be closed (and disabled).
 */
void omap_abe_port_close(struct abe *abe, struct omap_abe_port *port)
{
	/* disable the port */
	omap_abe_port_disable(abe, port);

	spin_lock(&abe->lock);
	list_del(&port->list);
	spin_unlock(&abe->lock);

	kfree(port);
}
EXPORT_SYMBOL(omap_abe_port_close);
