/*******************************************************************************

  LLDP Agent Daemon (LLDPAD) Software
  Copyright(c) 2007-2010 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-eedc Mailing List <e1000-eedc@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include "linux/if.h"
#include "lldp.h"
#include "dcb_types.h"
#include "lldp_dcbx.h"
#include "dcb_protocol.h"
#include "tlv_dcbx.h"
#include "messages.h"
#include "lldpad.h"
#include "libconfig.h"
#include "config.h"
#include "clif_msgs.h"
#include "lldp_mod.h"
#include "lldp_mand_clif.h"
#include "lldp/ports.h"
#include "lldp/states.h"
#include "lldp_dcbx_nl.h"
#include "lldp_dcbx_cfg.h"
#include "lldp_dcbx_cmds.h"
#include "lldp_rtnl.h"
#include "lldp_tlv.h"
#include "lldp_rtnl.h"

extern u8 gdcbx_subtype;

void dcbx_free_tlv(struct dcbx_tlvs *tlvs);
static int dcbx_check_operstate(struct port *port);

const struct lldp_mod_ops dcbx_ops = {
	.lldp_mod_register 	= dcbx_register,
	.lldp_mod_unregister 	= dcbx_unregister,
	.lldp_mod_gettlv	= dcbx_gettlv,
	.lldp_mod_rchange	= dcbx_rchange,
	.lldp_mod_ifup		= dcbx_ifup,
	.lldp_mod_ifdown	= dcbx_ifdown,
	.lldp_mod_mibdelete	= dcbx_mibDeleteObjects,
	.client_cmd		= dcbx_clif_cmd,
	.get_arg_handler	= dcbx_get_arg_handlers,
	.timer			= dcbx_check_operstate,
};

static int dcbx_check_operstate(struct port *port)
{
	int err;
	u8 app_good = 0;
	u8 pfc_good = 0;
	app_attribs app_data;
	pfc_attribs pfc_data;

	if (!port->portEnabled || !port->timers.dormantDelay)
		return 0;

	port->timers.dormantDelay--;

	err = get_app(port->ifname, 0, &app_data);
	if (err)
		goto err_out;
	err = get_pfc(port->ifname, &pfc_data);
	if (err)
		goto err_out;

	if ((pfc_data.protocol.Enable && pfc_data.protocol.OperMode) ||
	    !pfc_data.protocol.Enable)
		pfc_good = 1;
	if ((app_data.protocol.Enable && app_data.protocol.OperMode) ||
	    !app_data.protocol.Enable)
		app_good = 1;

	if ((pfc_good && app_good) || port->timers.dormantDelay == 1) {
		LLDPAD_DBG("%s: %s: IF_OPER_UP delay, %u pfc oper %u"
			   "app oper %u\n",
			__func__, port->ifname, port->timers.dormantDelay,
			pfc_data.protocol.OperMode,
			app_data.protocol.OperMode);
		port->timers.dormantDelay = 0;
		set_operstate(port->ifname, IF_OPER_UP);
	}

	return 0;

err_out:
	return -1;
}

struct dcbx_tlvs *dcbx_data(const char *ifname)
{
	struct dcbd_user_data *dud;
	struct dcbx_tlvs *tlv = NULL;

	dud = find_module_user_data_by_if(ifname, &lldp_head, LLDP_MOD_DCBX);
	if (dud) {
		LIST_FOREACH(tlv, &dud->head, entry) {
			if (!strncmp(tlv->ifname, ifname, IFNAMSIZ))
				return tlv;
		}
	}
	
	return NULL;
}

int dcbx_bld_tlv(struct port *newport)
{
	bool success;
	struct dcbx_tlvs *tlvs;
	int enabletx;
	long adminstatus = disabled;

	tlvs = dcbx_data(newport->ifname);

	get_config_setting(newport->ifname, ARG_ADMINSTATUS,
			  (void *)&adminstatus, CONFIG_TYPE_INT);

	enabletx = is_tlv_txenabled(newport->ifname, (OUI_CEE_DCBX << 8) |
				  tlvs->dcbx_st);

	if (!enabletx || adminstatus != enabledRxTx)
		return 0;

	tlvs->control = bld_dcbx_ctrl_tlv(tlvs);
	if (tlvs->control == NULL) {
		LLDPAD_INFO("add_port:  bld_dcbx_ctrl_tlv failed\n");
		goto fail_add;
	}

	if (tlvs->dcbx_st == dcbx_subtype2) {
		tlvs->pg2 = bld_dcbx2_pg_tlv(tlvs, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx2_pg_tlv: failed\n");
			goto fail_add;
		}
	} else {
		tlvs->pg1 = bld_dcbx1_pg_tlv(tlvs, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx1_pg_tlv: failed\n");
			goto fail_add;
		}
	}

	if (tlvs->dcbx_st == dcbx_subtype2) {
		tlvs->pfc2 = bld_dcbx2_pfc_tlv(tlvs, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx2_pfc_tlv: failed\n");
			goto fail_add;
		}
	} else {
		tlvs->pfc1 = bld_dcbx1_pfc_tlv(tlvs, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx1_pfc_tlv: failed\n");
			goto fail_add;
		}
	}

	if (tlvs->dcbx_st == dcbx_subtype2) {
		tlvs->app2 = bld_dcbx2_app_tlv(tlvs, 0, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx2_app_tlv: failed\n");
			goto fail_add;
		}
	} else {
		tlvs->app1 = bld_dcbx1_app_tlv(tlvs, 0, &success);
		if (!success) {
			LLDPAD_INFO("bld_dcbx1_app_tlv: failed\n");
			goto fail_add;
		}
	}

	tlvs->llink = bld_dcbx_llink_tlv(tlvs, LLINK_FCOE_STYPE,
						&success);
	if (!success) {
		LLDPAD_INFO("bld_dcbx_llink_tlv: failed\n");
		goto fail_add;
	}

	if (tlvs->dcbx_st == dcbx_subtype2) {
		tlvs->dcbx2 = bld_dcbx2_tlv(tlvs);
		if (tlvs->dcbx2 == NULL) {
			LLDPAD_INFO("add_port:  bld_dcbx2_tlv failed\n");
			goto fail_add;
		}
	} else {
		tlvs->dcbx1 = bld_dcbx1_tlv(tlvs);
		if (tlvs->dcbx1 == NULL) {
			LLDPAD_INFO("add_port:  bld_dcbx1_tlv failed\n");
			goto fail_add;
		}
	}
	return 0;
fail_add:
	return -1;
}

void dcbx_free_manifest(struct dcbx_manifest *manifest)
{
	if (!manifest)
		return;
	
	if (manifest->dcbx1)
		manifest->dcbx1 = free_unpkd_tlv(manifest->dcbx1);
	if (manifest->dcbx2)
		manifest->dcbx2 = free_unpkd_tlv(manifest->dcbx2);
	if (manifest->dcbx_ctrl)
		manifest->dcbx_ctrl = free_unpkd_tlv(manifest->dcbx_ctrl);
	if (manifest->dcbx_pg)
		manifest->dcbx_pg = free_unpkd_tlv(manifest->dcbx_pg);
	if (manifest->dcbx_pfc)
		manifest->dcbx_pfc = free_unpkd_tlv(manifest->dcbx_pfc);
	if (manifest->dcbx_app)
		manifest->dcbx_app = free_unpkd_tlv(manifest->dcbx_app);
	if (manifest->dcbx_llink)
		manifest->dcbx_llink = free_unpkd_tlv(manifest->dcbx_llink);

	return;
}

void dcbx_free_tlv(struct dcbx_tlvs *tlvs)
{
	if (!tlvs)
		return;

	if (tlvs->control != NULL) {
		tlvs->control = free_unpkd_tlv(tlvs->control);
	}

	if (tlvs->pg1 != NULL) {
		tlvs->pg1 = free_unpkd_tlv(tlvs->pg1);
	}

	if (tlvs->pg2 != NULL) {
		tlvs->pg2 = free_unpkd_tlv(tlvs->pg2);
	}

	if (tlvs->pfc1 != NULL) {
		tlvs->pfc1 = free_unpkd_tlv(tlvs->pfc1);
	}

	if (tlvs->pfc2 != NULL) {
		tlvs->pfc2 = free_unpkd_tlv(tlvs->pfc2);
	}

	if (tlvs->app1 != NULL) {
		tlvs->app1 = free_unpkd_tlv(tlvs->app1);
	}

	if (tlvs->app2 != NULL)
		tlvs->app2 = free_unpkd_tlv(tlvs->app2);

	if (tlvs->llink != NULL) {
		tlvs->llink = free_unpkd_tlv(tlvs->llink);
	}

	if (tlvs->dcbx1 != NULL) {
		tlvs->dcbx1 = free_unpkd_tlv(tlvs->dcbx1);
	}

	if (tlvs->dcbx2 != NULL) {
		tlvs->dcbx2 = free_unpkd_tlv(tlvs->dcbx2);
	}
	return;
}

struct packed_tlv* dcbx_gettlv(struct port *port)
{
	struct packed_tlv *ptlv = NULL;
	struct dcbx_tlvs *tlvs;

        if (!check_port_dcb_mode(port->ifname))
		return NULL;

	tlvs = dcbx_data(port->ifname);

	dcbx_free_tlv(tlvs);
	if (!tlvs)
		return NULL;

	dcbx_bld_tlv(port);

	if (tlvs->dcbx_st == dcbx_subtype2) {
		/* Load Type127 - dcbx subtype 2*/
		if (tlv_ok(tlvs->dcbx2))
			ptlv =  pack_tlv(tlvs->dcbx2);
	} else {
		/* Load Type127 - dcbx subtype1 */
		if (tlv_ok(tlvs->dcbx1))
			ptlv =  pack_tlv(tlvs->dcbx1);
	}

	return ptlv;
}

static void dcbx_free_data(struct dcbd_user_data *dud)
{
	struct dcbx_tlvs *dd;
	if (dud) {
		while (!LIST_EMPTY(&dud->head)) {
			dd = LIST_FIRST(&dud->head);
			LIST_REMOVE(dd, entry);
			dcbx_free_tlv(dd);
			dcbx_free_manifest(dd->manifest);
			free(dd->manifest);
			free(dd);
		}
	}
}

struct lldp_module * dcbx_register(void)
{
	struct lldp_module *mod;
	struct dcbd_user_data *dud;
	int dcbx_version;
	int i;

	dcbx_default_cfg_file();

	/* Get the DCBX version */
	if (get_dcbx_version(&dcbx_version)) {
		gdcbx_subtype = dcbx_version;
	} else {
		LLDPAD_ERR("failed to get DCBX version");
		goto out_err;
	}

	mod = malloc(sizeof(struct lldp_module));
	if (!mod) {
		LLDPAD_ERR("failed to malloc LLDP DCBX module data\n");
		goto out_err;
	}
	dud = malloc(sizeof(*dud));
	if (!dud) {
		free(mod);
		LLDPAD_ERR("failed to malloc LLDP DCBX module user data\n");
		goto out_err;
	}

	LIST_INIT(&dud->head);
	mod->id = LLDP_MOD_DCBX;
	mod->ops = &dcbx_ops;
	mod->data = dud;

	/* store pg defaults */
	if (!add_pg_defaults()) {
		LLDPAD_INFO("failed to add default PG data");
		goto out_err;
	}

	/* store pg defaults */
	if (!add_pfc_defaults()) {
		LLDPAD_INFO("failed to add default PFC data");
		goto out_err;
	}

	/* store app defaults */
	for (i = 0; i < DCB_MAX_APPTLV; i++) {
		if (!add_app_defaults(i)) {
			LLDPAD_INFO("failed to add default APP data %d", i);
			goto out_err;
		}
	}


	for (i = 0; i < DCB_MAX_LLKTLV; i++) {
		if (!add_llink_defaults(i)) {
			LLDPAD_INFO("failed to add default APP data %i", i);
			goto out_err;
		}
	}

	if (!init_drv_if()) {
		LLDPAD_WARN("Error creataing netlink socket for driver i/f.\n");
		goto out_err;
	}

	LLDPAD_DBG("%s: dcbx register done\n", __func__);
	return mod;
out_err:
	LLDPAD_DBG("%s: dcbx register failed\n", __func__);
	return NULL;
}

/* BUG: need to check if tlvs are freed */
void dcbx_unregister(struct lldp_module *mod)
{
	dcbx_remove_all();
	deinit_drv_if();
	if (mod->data) {
		dcbx_free_data((struct dcbd_user_data *) mod->data);
		free(mod->data);
	}
	free(mod);
	LLDPAD_DBG("%s: unregister dcbx complete.\n", __func__);
}

void dcbx_ifup(char *ifname)
{
	struct port *port = NULL;
	struct dcbx_tlvs *tlvs;
	struct dcbd_user_data *dud;
	struct dcbx_manifest *manifest;
	int dcb_enable;
	long adminstatus;
	long enabletx;
	char arg_path[256];

	/* dcb does not support bonded devices */
	if (is_bond(ifname) || is_vlan(ifname))
		return;

	if (!get_dcb_enable_state(ifname, &dcb_enable))
		set_hw_state(ifname, dcb_enable);

	port = porthead;
	while (port != NULL) {
		if (!strncmp(ifname, port->ifname, MAX_DEVICE_NAME_LEN))
			break;
		port = port->next;
	}

	dud = find_module_user_data_by_if(ifname, &lldp_head, LLDP_MOD_DCBX);
	tlvs = dcbx_data(ifname);	

	if (!port || !check_port_dcb_mode(ifname)) 
		return;
	else if (tlvs)
		goto initialized;

	/* if no adminStatus setting or wrong setting for adminStatus,
	 * then set adminStatus to enabledRxTx.
	 */
	if (get_config_setting(ifname, ARG_ADMINSTATUS, (void *)&adminstatus,
				CONFIG_TYPE_INT) ||
				adminstatus == enabledTxOnly ||
				adminstatus == enabledRxOnly) {

		/* set enableTx to true if it is not already set */
		snprintf(arg_path, sizeof(arg_path), "%s%08x.%s", TLVID_PREFIX,
			(OUI_CEE_DCBX << 8) | 1, ARG_TLVTXENABLE);
		if (get_config_setting(ifname, arg_path,
				(void *)&enabletx, CONFIG_TYPE_BOOL)) {
			enabletx = true;
			set_config_setting(ifname, arg_path,
			              (void *)&enabletx, CONFIG_TYPE_BOOL);
		}

		snprintf(arg_path, sizeof(arg_path), "%s%08x.%s", TLVID_PREFIX,
			(OUI_CEE_DCBX << 8) | 2, ARG_TLVTXENABLE);
		if (get_config_setting(ifname, arg_path,
				(void *)&enabletx, CONFIG_TYPE_BOOL)) {
			enabletx = true;
			set_config_setting(ifname, arg_path,
			              (void *)&enabletx, CONFIG_TYPE_BOOL);
		}

		adminstatus = enabledRxTx;
		if (set_config_setting(ifname, ARG_ADMINSTATUS,
			              (void *)&adminstatus, CONFIG_TYPE_INT) ==
				       cmd_success)
			set_lldp_port_admin(ifname, (int)adminstatus);
	}

	tlvs = malloc(sizeof(*tlvs));
	if (!tlvs) {
		LLDPAD_DBG("%s: ifname %s malloc failed.\n", __func__, ifname);
		return;
	}
	memset(tlvs, 0, sizeof(*tlvs));
		
	manifest = malloc(sizeof(*manifest));
	if (!manifest) {
		free(tlvs);
		LLDPAD_DBG("%s: %s malloc failure\n", __func__, ifname);
		return;
	}
	memset(manifest, 0, sizeof(*manifest));

	tlvs->manifest = manifest;
	strncpy(tlvs->ifname, ifname, IFNAMSIZ);
	tlvs->port = port;
	tlvs->dcbdu = 0;
	tlvs->dcbx_st = gdcbx_subtype;
	LIST_INSERT_HEAD(&dud->head, tlvs, entry);		

	dcbx_add_adapter(ifname);
	/* ensure advertise bits are set consistently with enabletx */
	enabletx = is_tlv_txenabled(ifname, (OUI_CEE_DCBX << 8) |
				    tlvs->dcbx_st);
	if (!enabletx)
		dont_advertise_dcbx_all(ifname);
	dcbx_bld_tlv(port);

initialized:
	if (get_operstate(ifname) == IF_OPER_UP)
		set_hw_all(ifname);

	return;
}

void dcbx_ifdown(char *device_name)
{
	struct port *port = NULL;
	struct dcbx_tlvs *tlvs;

	/* dcb does not support bonded devices */
	if (is_bond(device_name))
		return;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(device_name, port->ifname, MAX_DEVICE_NAME_LEN))
			break;
		port = port->next;
	}

	tlvs = dcbx_data(device_name);

	if (!tlvs)
		return;

	/* remove dcb port */
	if (check_port_dcb_mode(device_name)) {
		dcbx_remove_adapter(device_name);
	}

	if (tlvs) {
		LIST_REMOVE(tlvs, entry);
		dcbx_free_tlv(tlvs);
		dcbx_free_manifest(tlvs->manifest);
		free(tlvs->manifest);
		free(tlvs);
	}
}

void clear_dcbx_manifest(struct dcbx_tlvs *dcbx)
{
	if (!dcbx)
		return;

	if (dcbx->manifest->dcbx_llink)
		dcbx->manifest->dcbx_llink =
			free_unpkd_tlv(dcbx->manifest->dcbx_llink);
	if (dcbx->manifest->dcbx_app)
		dcbx->manifest->dcbx_app =
			free_unpkd_tlv(dcbx->manifest->dcbx_app);
	if (dcbx->manifest->dcbx_pfc)
		dcbx->manifest->dcbx_pfc =
			free_unpkd_tlv(dcbx->manifest->dcbx_pfc);
	if (dcbx->manifest->dcbx_pg)
		dcbx->manifest->dcbx_pg =
			free_unpkd_tlv(dcbx->manifest->dcbx_pg);
	if (dcbx->manifest->dcbx_ctrl)
		dcbx->manifest->dcbx_ctrl =
			free_unpkd_tlv(dcbx->manifest->dcbx_ctrl);
	if (dcbx->manifest->dcbx1)
		dcbx->manifest->dcbx1 =
			free_unpkd_tlv(dcbx->manifest->dcbx1);
	if (dcbx->manifest->dcbx2)
		dcbx->manifest->dcbx2 =
			free_unpkd_tlv(dcbx->manifest->dcbx2);
	free(dcbx->manifest);
	dcbx->manifest = NULL;
}


/*
 * dcbx_rchange: process RX TLV LLDPDU
 *
 * TLV not consumed on error otherwise it is either free'd or stored
 * internally in the module.
 */
int dcbx_rchange(struct port *port,  struct unpacked_tlv *tlv)
{
	u8 oui[DCB_OUI_LEN] = INIT_DCB_OUI;
	struct dcbx_tlvs *dcbx;
	struct dcbx_manifest *manifest;

	dcbx = dcbx_data(port->ifname);

	if (!dcbx)
		return SUBTYPE_INVALID;

	/* 
 	 * TYPE_1 is _mandatory_ and will always be before the 
 	 * DCBX TLV so we can use it to mark the begining of a
 	 * pdu for dcbx to verify only a single DCBX TLV is 
 	 * present
 	 */ 
	if (tlv->type == TYPE_1) {
		manifest = malloc(sizeof(*manifest));
		memset(manifest, 0, sizeof(*manifest));
		dcbx->manifest = manifest;
		dcbx->dcbdu = 0;
	}

	if (tlv->type == TYPE_127) {
		if (tlv->length < (DCB_OUI_LEN + OUI_SUBTYPE_LEN)) {
			return TLV_ERR;
		}

		/* expect oui_st to match tlv_info. */
		if ((memcmp(tlv->info, &oui, DCB_OUI_LEN) != 0)) {
			assert(port->tlvs.cur_peer == NULL);
			/* not a DCBX TLV */
			return SUBTYPE_INVALID;
		}

		if (dcbx->dcbx_st == dcbx_subtype2) {
			if ((tlv->info[DCB_OUI_LEN] == dcbx_subtype2)
				&& (port->lldpdu & RCVD_LLDP_DCBX2_TLV)){
				LLDPAD_INFO("Received duplicate DCBX TLVs\n");
				return TLV_ERR;
			}
		}
		if ((tlv->info[DCB_OUI_LEN] == dcbx_subtype1)
			&& (port->lldpdu & RCVD_LLDP_DCBX1_TLV)) {
			LLDPAD_INFO("Received duplicate DCBX TLVs\n");
			return TLV_ERR;
		}

		if ((dcbx->dcbx_st == dcbx_subtype2) &&
			(tlv->info[DCB_OUI_LEN] == dcbx_subtype2)) {
			port->lldpdu |= RCVD_LLDP_DCBX2_TLV;
			dcbx->manifest->dcbx2 = tlv;
		} else if (tlv->info[DCB_OUI_LEN] == dcbx_subtype1) {
			port->lldpdu |= RCVD_LLDP_DCBX1_TLV;
			dcbx->manifest->dcbx1 = tlv;
		} else {
			/* not a DCBX subtype we support */
			return SUBTYPE_INVALID;
		}
	}

	if (tlv->type == TYPE_0) {
		/*  unpack highest dcbx subtype first, to allow lower
		 *  subtype to store tlvs if higher was not present
		 */
		if ((dcbx->dcbx_st == dcbx_subtype2) &&
			 dcbx->manifest->dcbx2) {
			 load_peer_tlvs(port, dcbx->manifest->dcbx2,
				CURRENT_PEER);
			if (unpack_dcbx2_tlvs(port,
				dcbx->manifest->dcbx2) == false) {
				LLDPAD_DBG("Error unpacking the DCBX2"
					"TLVs - Discarding LLDPDU\n");
				return TLV_ERR;
			}
		}
		if (dcbx->manifest->dcbx1) {
			if (!dcbx->manifest->dcbx2)
				load_peer_tlvs(port,
					dcbx->manifest->dcbx1,
					CURRENT_PEER);
			if (unpack_dcbx1_tlvs(port,
				dcbx->manifest->dcbx1) == false) {
				LLDPAD_DBG("Error unpacking the DCBX1"
					"TLVs - Discarding LLDPDU\n");
				return TLV_ERR;
			}
		}
		if (port->tlvs.cur_peer) {
			process_dcbx_tlv(port, port->tlvs.cur_peer);
		}
		free_unpkd_tlv(tlv);

		clear_dcbx_manifest(dcbx);
		dcbx->dcbdu = 0;
	}

	return TLV_OK;
}

u8 dcbx_mibDeleteObjects(struct port *port)
{
	control_protocol_attribs  peer_control;
	pg_attribs  peer_pg;
	pfc_attribs peer_pfc;
	app_attribs peer_app;
	llink_attribs peer_llink;
	u32 subtype = 0;
	u32 EventFlag = 0;
	int i;

	/* Set any stored values for this TLV to !Present */
	if (get_peer_pg(port->ifname, &peer_pg) == dcb_success) {
		if (peer_pg.protocol.TLVPresent == true) {
			peer_pg.protocol.TLVPresent = false;
			put_peer_pg(port->ifname, &peer_pg);
			DCB_SET_FLAGS(EventFlag, DCB_REMOTE_CHANGE_PG);
		}
	} else {
		return (u8)-1;
	}

	if (get_peer_pfc(port->ifname, &peer_pfc) == dcb_success) {
		if (peer_pfc.protocol.TLVPresent == true) {
			peer_pfc.protocol.TLVPresent = false;
			put_peer_pfc(port->ifname, &peer_pfc);
			DCB_SET_FLAGS(EventFlag, DCB_REMOTE_CHANGE_PFC);
		}
	} else {
		return (u8)-1;
	}

	for (i = 0; i < DCB_MAX_APPTLV; i++) {
		if (get_peer_app(port->ifname, i, &peer_app) ==
			dcb_success) {
			if (peer_app.protocol.TLVPresent == true) {
				peer_app.protocol.TLVPresent = false;
				peer_app.Length = 0;
				put_peer_app(port->ifname, i, &peer_app);
				DCB_SET_FLAGS(EventFlag, DCB_REMOTE_CHANGE_APPTLV(i));
			}
		} else {
			return (u8)-1;
  		}
	}

	if (get_peer_llink(port->ifname, subtype, &peer_llink) == dcb_success) {
		if (peer_llink.protocol.TLVPresent == true) {
			peer_llink.protocol.TLVPresent = false;
			put_peer_llink(port->ifname, subtype, &peer_llink);
			DCB_SET_FLAGS(EventFlag, DCB_REMOTE_CHANGE_LLINK);
		}
	} else {
		return (u8)-1;
	}

	if (get_peer_control(port->ifname, &peer_control) ==
		dcb_success) {
		peer_control.RxDCBTLVState = DCB_PEER_EXPIRED;
		put_peer_control(port->ifname, &peer_control);
	} else {
		return (u8)-1;
	}

	if (EventFlag != 0) {
		/* process for all subtypes */
		run_dcb_protocol(port->ifname, EventFlag, DCB_MAX_APPTLV+1);
		EventFlag = 0;
	}
	return 0;
}

