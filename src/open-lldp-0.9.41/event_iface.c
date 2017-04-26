/*******************************************************************************

  LLDP Agent Daemon (LLDPAD) Software
  Copyright(c) 2007-2010 Intel Corporation.

  implementation of libvirt netlink interface
  (c) Copyright IBM Corp. 2010

  Author(s): Jens Osterkamp <jens at linux.vnet.ibm.com>
	     Stefan Berger <stefanb at linux.vnet.ibm.com>
	     Gerhard Stenzel <gstenzel at linux.vnet.ibm.com>

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
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <linux/sockios.h>
#include <netlink/attr.h>
#include <netlink/msg.h>
#include <syslog.h>
#include <unistd.h>
#include "linux/if.h"
#include "linux/if_vlan.h"
#include "linux/rtnetlink.h"
#include "linux/if_link.h"
#include "lldpad.h"
#include "lldp_mod.h"
#include "eloop.h"
#include "event_iface.h"
#include "lldp_util.h"
#include "config.h"
#include "lldp/l2_packet.h"
#include "config.h"
#include "lldp/states.h"
#include "messages.h"
#include "lldp_rtnl.h"
#include "lldp_vdp.h"
#include "lldp_tlv.h"

#define MAX_PAYLOAD 4096 /* maximum payload size */

static struct nla_policy ifla_vf_policy[IFLA_VF_MAX + 1] =
{
	[IFLA_VF_MAC] = { .minlen = sizeof(struct ifla_vf_mac),
			  .maxlen = sizeof(struct ifla_vf_mac)},
	[IFLA_VF_VLAN] = { .minlen = sizeof(struct ifla_vf_vlan),
			   .maxlen = sizeof(struct ifla_vf_vlan)},
};

static struct nla_policy ifla_vf_ports_policy[IFLA_VF_PORT_MAX + 1] =
{
	[IFLA_VF_PORT] = { .type = NLA_NESTED },
};

static struct nla_policy ifla_port_policy[IFLA_PORT_MAX + 1] =
{
	[IFLA_PORT_VF]            = { .type = NLA_U32 },
	[IFLA_PORT_PROFILE]       = { .type = NLA_STRING },
	[IFLA_PORT_VSI_TYPE]      = { .minlen = sizeof(struct ifla_port_vsi) },
	[IFLA_PORT_INSTANCE_UUID] = { .minlen = PORT_UUID_MAX,
				      .maxlen = PORT_UUID_MAX, },
	[IFLA_PORT_HOST_UUID]     = { .minlen = PORT_UUID_MAX,
				      .maxlen = PORT_UUID_MAX, },
	[IFLA_PORT_REQUEST]       = { .type = NLA_U8  },
	[IFLA_PORT_RESPONSE]      = { .type = NLA_U16 },
};

static void event_if_decode_rta(int type, struct rtattr *rta, int *ls, char *d)
{

	LLDPAD_DBG("    rta_type  =\n", rta->rta_len);
	
	switch (type) {
	case IFLA_ADDRESS:
		LLDPAD_DBG(" IFLA_ADDRESS\n");
		break;
	case IFLA_BROADCAST:
		LLDPAD_DBG(" IFLA_BROADCAST\n");
		break;
	case IFLA_OPERSTATE:
		LLDPAD_DBG(" IFLA_OPERSTATE \n", type);
		*ls = (*((int *)RTA_DATA(rta)));
		break;
	case IFLA_LINKMODE:
		LLDPAD_DBG(" IFLA_LINKMODE \n", type);
		LLDPAD_DBG("        LINKMODE = \n", (*((int *)RTA_DATA(rta)))?
			"IF_LINK_MODE_DORMANT": "IF_LINK_MODE_DEFAULT");
		break;
	case IFLA_IFNAME:
		strncpy(d, (char *)RTA_DATA(rta), IFNAMSIZ);
		LLDPAD_DBG(" IFLA_IFNAME\n");
		LLDPAD_DBG(" device name is \n", d);
		break;
	default:
		LLDPAD_DBG(" unknown type : \n", type);
		break;
	}
}

int oper_add_device(char *device_name)
{
	struct lldp_module *np;
	const struct lldp_mod_ops *ops;
	struct port *port;
	int err;

	port = porthead;
	while (port != NULL) {
		if (!strncmp(device_name, port->ifname, MAX_DEVICE_NAME_LEN))
			break;
		port = port->next;
	}

	if (!port) {
		if (is_bond(device_name))
			err = add_bond_port(device_name);
		else
			err = add_port(device_name);

		if (err) {
			LLDPAD_INFO("%s: Error adding device %s\n",
				__func__, device_name);
			return err;
		} else
			LLDPAD_INFO("%s: Adding device %s\n",
				__func__, device_name);
	} else if (!port->portEnabled)
		reinit_port(device_name);

	LIST_FOREACH(np, &lldp_head, lldp) {
		ops = np->ops;
		if (ops->lldp_mod_ifup)
			ops->lldp_mod_ifup(device_name);
	}

	set_lldp_port_enable_state(device_name, 1);
	return 0;
}

static void event_if_decode_nlmsg(int route_type, void *data, int len)
{
	struct lldp_module *np;
	const struct lldp_mod_ops *ops;
	struct rtattr *rta;
	char device_name[IFNAMSIZ];
	int attrlen;
	int valid;
	int link_status = IF_OPER_UNKNOWN;

	switch (route_type) {
	case RTM_NEWLINK:		
	case RTM_DELLINK:
	case RTM_SETLINK:
	case RTM_GETLINK:
		LLDPAD_DBG("  IFINFOMSG\n");
		LLDPAD_DBG("  ifi_family = \n",
			((struct ifinfomsg *)data)->ifi_family);
		LLDPAD_DBG("  ifi_type   = \n",
			((struct ifinfomsg *)data)->ifi_type);
		LLDPAD_DBG("  ifi_index  = \n",
			((struct ifinfomsg *)data)->ifi_index);
		LLDPAD_DBG("  ifi_flags  = \n",
			((struct ifinfomsg *)data)->ifi_flags);
		LLDPAD_DBG("  ifi_change = \n",
			((struct ifinfomsg *)data)->ifi_change);

		/* print attributes */
		rta = IFLA_RTA(data);

		attrlen = len - sizeof(struct ifinfomsg);
		while (RTA_OK(rta, attrlen)) {
			event_if_decode_rta(rta->rta_type, rta,
					    &link_status, device_name);
			rta = RTA_NEXT(rta, attrlen);
		}

		LLDPAD_DBG("link status: \n", link_status);
		LLDPAD_DBG("device name: \n", device_name);

		switch (link_status) {
		case IF_OPER_DOWN:
			LLDPAD_DBG("******* LINK DOWN: %s\n", device_name);

			valid = is_valid_lldp_device(device_name);
			if (!valid)
				break;

			LIST_FOREACH(np, &lldp_head, lldp) {
				ops = np->ops;
				if (ops->lldp_mod_ifdown)
					ops->lldp_mod_ifdown(device_name);
			}

			/* Disable Port */
			set_lldp_port_enable_state(device_name, 0);

			if (route_type == RTM_DELLINK) {
				LLDPAD_INFO("%s: %s: device removed!\n",
					__func__, device_name);
				remove_port(device_name);
			}
			break;
		case IF_OPER_DORMANT:
			LLDPAD_DBG("******* LINK DORMANT: %s\n", device_name);
			valid = is_valid_lldp_device(device_name);
			if (!valid)
				break;
			set_port_oper_delay(device_name);
			oper_add_device(device_name);
			break;
		case IF_OPER_UP:
			LLDPAD_DBG("******* LINK UP: %s\n", device_name);
			valid = is_valid_lldp_device(device_name);
			if (!valid)
				break;
			oper_add_device(device_name);
			break;
		default:
			break;
		}
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_GETADDR:
		LLDPAD_DBG("Address change.\n");
		break;
	default:
		LLDPAD_DBG("No decode for this type\n");
	}
}

static void event_if_process_recvmsg(struct nlmsghdr *nlmsg)
{
	LLDPAD_DBG("%s:%s: nlmsg_type: %d\n", __FILE__, __FUNCTION__, nlmsg->nlmsg_type);
	event_if_decode_nlmsg(nlmsg->nlmsg_type, NLMSG_DATA(nlmsg),
		NLMSG_PAYLOAD(nlmsg, 0));
}

static int event_if_parse_getmsg(struct nlmsghdr *nlh, int *ifindex,
				 char *ifname)
{
	struct nlattr *tb[IFLA_MAX+1];
	struct ifinfomsg *ifinfo;

	if (nlmsg_parse(nlh, sizeof(struct ifinfomsg),
			(struct nlattr **)&tb, IFLA_MAX, NULL)) {
		LLDPAD_ERR("Error parsing GETLINK request...\n");
		return -EINVAL;
	}

	if (tb[IFLA_IFNAME]) {
		ifname = (char *)RTA_DATA(tb[IFLA_IFNAME]);
		LLDPAD_DBG("IFLA_IFNAME=%s\n", ifname);
	} else {
		ifinfo = (struct ifinfomsg *)NLMSG_DATA(nlh);
		*ifindex = ifinfo->ifi_index;
		LLDPAD_DBG("interface index: %d\n", ifinfo->ifi_index);
	}

	return 0;
}

static int event_if_parse_setmsg(struct nlmsghdr *nlh)
{
	struct nlattr *tb[IFLA_MAX+1],
		      *tb3[IFLA_PORT_MAX+1],
		      *tb_vfinfo[IFLA_VF_MAX+1],
		      *tb_vfinfo_list;
	struct vsi_profile *profile, *p;
	struct ifinfomsg *ifinfo;
	struct vdp_data *vd;
	char *ifname;
	int rem;

	if (nlmsg_parse(nlh, sizeof(struct ifinfomsg),
			(struct nlattr **)&tb, IFLA_MAX, NULL)) {
		LLDPAD_ERR("Error parsing request...\n");
		return -EINVAL;
	}

	LLDPAD_DBG("%s(%d): nlmsg_len %i\n", __FILE__, __LINE__, nlh->nlmsg_len);

	if (tb[IFLA_IFNAME]) {
		ifname = (char *)RTA_DATA(tb[IFLA_IFNAME]);
	} else {
		ifname = malloc(IFNAMSIZ);
		ifinfo = (struct ifinfomsg *)NLMSG_DATA(nlh);
		LLDPAD_DBG("interface index: %d\n", ifinfo->ifi_index);
		if (!if_indextoname(ifinfo->ifi_index, ifname)) {
			LLDPAD_ERR("Could not find name for interface %i !\n", ifinfo->ifi_index);
			return -ENXIO;
		}
	}

	LLDPAD_DBG("IFLA_IFNAME=%s\n", ifname);

	vd = vdp_data(ifname);
	if (!vd) {
		LLDPAD_ERR("interface %s has not yet been configured !\n", ifname);
		return -ENXIO;
	}

	if (!tb[IFLA_VFINFO_LIST]) {
		LLDPAD_ERR("IFLA_VFINFO_LIST missing.\n");
		return -EINVAL;
	} else {
		LLDPAD_DBG("FOUND IFLA_VFINFO_LIST!\n");
	}

	nla_for_each_nested(tb_vfinfo_list, tb[IFLA_VFINFO_LIST], rem) {
		if (nla_type(tb_vfinfo_list) != IFLA_VF_INFO) {
			LLDPAD_ERR("nested parsing of IFLA_VFINFO_LIST failed.\n");
			return -EINVAL;
		}

		if (nla_parse_nested(tb_vfinfo, IFLA_VF_MAX, tb_vfinfo_list,
				     ifla_vf_policy)) {
			LLDPAD_ERR("nested parsing of IFLA_VF_INFO failed.\n");
			return -EINVAL;
		}
	}

	profile = malloc(sizeof(struct vsi_profile));
	if (!profile)
		return -ENOMEM;
	memset(profile, 0, sizeof(struct vsi_profile));

	if (tb_vfinfo[IFLA_VF_MAC]) {
		struct ifla_vf_mac *mac = RTA_DATA(tb_vfinfo[IFLA_VF_MAC]);
		u8 *m = mac->mac;
		LLDPAD_DBG("IFLA_VF_MAC=%2x:%2x:%2x:%2x:%2x:%2x\n",
			m[0], m[1], m[2], m[3], m[4], m[5]);
		memcpy(&profile->mac, m, ETH_ALEN);
	}

	if (tb_vfinfo[IFLA_VF_VLAN]) {
		struct ifla_vf_vlan *vlan = RTA_DATA(tb_vfinfo[IFLA_VF_VLAN]);
		LLDPAD_DBG("IFLA_VF_VLAN=%d\n", vlan->vlan);
		profile->vlan = (u16) vlan->vlan;
	}

	if (tb[IFLA_VF_PORTS]) {
		struct nlattr *tb_vf_ports;

		LLDPAD_DBG("FOUND IFLA_VF_PORTS\n");

		nla_for_each_nested(tb_vf_ports, tb[IFLA_VF_PORTS], rem) {

			LLDPAD_DBG("ITERATING\n");

			if (nla_type(tb_vf_ports) != IFLA_VF_PORT) {
				LLDPAD_DBG("not a IFLA_VF_PORT. skipping\n");
				continue;
			}

			if (nla_parse_nested(tb3, IFLA_PORT_MAX, tb_vf_ports,
					     ifla_port_policy)) {
				LLDPAD_ERR("nested parsing on level 2 failed.\n");
				return -EINVAL;
			}

			if (tb3[IFLA_PORT_VF]) {
				LLDPAD_DBG("IFLA_PORT_VF=%d\n", *(uint32_t*)(RTA_DATA(tb3[IFLA_PORT_VF])));
			}

			if (tb3[IFLA_PORT_PROFILE]) {
				LLDPAD_DBG("IFLA_PORT_PROFILE=%s\n", (char *)RTA_DATA(tb3[IFLA_PORT_PROFILE]));
			}

			if (tb3[IFLA_PORT_VSI_TYPE]) {
				struct ifla_port_vsi *pvsi;
				int tid = 0;

				pvsi = (struct ifla_port_vsi*)RTA_DATA(tb3[IFLA_PORT_VSI_TYPE]);
				tid = pvsi->vsi_type_id[2] << 16 |
					pvsi->vsi_type_id[1] << 8 |
					pvsi->vsi_type_id[0];

				LLDPAD_DBG("mgr_id : %d\n", pvsi->vsi_mgr_id);
				LLDPAD_DBG("type_id : %d\n", tid);
				LLDPAD_DBG("type_version : %d\n", pvsi->vsi_type_version);

				profile->mgrid = pvsi->vsi_mgr_id;
				profile->id = tid;
				profile->version = pvsi->vsi_type_version;
			}

			if (tb3[IFLA_PORT_INSTANCE_UUID]) {
				int i;
				unsigned char *uuid;
				uuid = (unsigned char *)RTA_DATA(tb3[IFLA_PORT_INSTANCE_UUID]);

				char instance[INSTANCE_STRLEN+2];
				instance2str(uuid, instance, sizeof(instance));
				LLDPAD_DBG("IFLA_PORT_INSTANCE_UUID=%s\n", instance);

				memcpy(&profile->instance,
				       RTA_DATA(tb3[IFLA_PORT_INSTANCE_UUID]), 16);
			}

			if (tb3[IFLA_PORT_REQUEST]) {
				LLDPAD_DBG("IFLA_PORT_REQUEST=%d\n",
					*(uint8_t*)RTA_DATA(tb3[IFLA_PORT_REQUEST]));
					profile->mode = *(uint8_t*)RTA_DATA(tb3[IFLA_PORT_REQUEST]);
			}

			if (tb3[IFLA_PORT_RESPONSE]) {
				LLDPAD_DBG("IFLA_PORT_RESPONSE=%d\n",
					*(uint16_t*)RTA_DATA(tb3[IFLA_PORT_RESPONSE]));
				profile->response = *(uint16_t*)RTA_DATA(tb3[IFLA_PORT_RESPONSE]);
			}
		}
	}

	if (ifname) {
		struct port *port = port_find_by_name(ifname);

		if (port) {
			profile->port = port;
		} else {
			LLDPAD_ERR("%s(%i): Could not find port for %s\n", __func__,
			       __LINE__, ifname);
			return -EEXIST;
		}
	}

	p = vdp_add_profile(profile);

	if (!p) {
		free(profile);
		return -EINVAL;
	}

	vdp_somethingChangedLocal(profile, VDP_PROFILE_REQ);
	vdp_vsi_sm_station(p);

	return 0;
}

static void event_if_parseResponseMsg(struct nlmsghdr *nlh)
{
	struct nlattr *tb[IFLA_MAX+1],
		      *tb2[IFLA_VF_PORT_MAX + 1],
		      *tb3[IFLA_PORT_MAX+1];

	if (nlmsg_parse(nlh, sizeof(struct ifinfomsg),
			(struct nlattr **)&tb, IFLA_MAX, NULL)) {
		LLDPAD_ERR("Error parsing netlink response...\n");
		return;
	}

	if (tb[IFLA_IFNAME]) {
		LLDPAD_DBG("IFLA_IFNAME=%s\n", (char *)RTA_DATA(tb[IFLA_IFNAME]));
	} else {
		struct ifinfomsg *ifinfo = (struct ifinfomsg *)NLMSG_DATA(nlh);
		LLDPAD_DBG("interface index: %d\n", ifinfo->ifi_index);
	}

	if (tb[IFLA_VF_PORTS]) {
			if (nla_parse_nested(tb2, IFLA_VF_PORT_MAX, tb[IFLA_VF_PORTS],
					     ifla_vf_ports_policy)) {
				LLDPAD_ERR("nested parsing on level 1 failed.\n");
				return;
			}

	        if (tb2[IFLA_VF_PORT]) {
			if (nla_parse_nested(tb3, IFLA_PORT_MAX, tb2[IFLA_VF_PORT],
					     ifla_port_policy)) {
				LLDPAD_ERR("nested parsing on level 2 failed.\n");
				return;
			}

			if (tb3[IFLA_PORT_VF]) {
				LLDPAD_DBG("IFLA_PORT_VF=%d\n", *(uint32_t*)(RTA_DATA(tb3[IFLA_PORT_VF])));
			}

			if (tb3[IFLA_PORT_PROFILE]) {
				LLDPAD_DBG("IFLA_PORT_PROFILE=%s\n", (char *)RTA_DATA(tb3[IFLA_PORT_PROFILE]));
			}

			if (tb3[IFLA_PORT_VSI_TYPE]) {
				struct ifla_port_vsi *pvsi;
				int tid = 0;
				pvsi = (struct ifla_port_vsi*)RTA_DATA(tb3[IFLA_PORT_VSI_TYPE]);
				tid = pvsi->vsi_type_id[2] << 16 |
					pvsi->vsi_type_id[1] << 8 |
					pvsi->vsi_type_id[0];
				LLDPAD_DBG("mgr_id : %d "
					"type_id : %d "
					"type_version : %d\n",
					pvsi->vsi_mgr_id,
					tid,
					pvsi->vsi_type_version);
			}

			if (tb3[IFLA_PORT_INSTANCE_UUID]) {
				int i;
				unsigned char *uuid;
				uuid = (unsigned char *)RTA_DATA(tb3[IFLA_PORT_INSTANCE_UUID]);

				char instance[INSTANCE_STRLEN+2];
				instance2str(uuid, instance, sizeof(instance));
				LLDPAD_DBG("IFLA_PORT_INSTANCE_UUID=%s\n", &instance[0]);
			}

			if (tb3[IFLA_PORT_REQUEST]) {
				LLDPAD_DBG("IFLA_PORT_REQUEST=%d\n",
					*(uint8_t*)RTA_DATA(tb3[IFLA_PORT_REQUEST]));
			}		
			if (tb3[IFLA_PORT_RESPONSE]) {
				LLDPAD_DBG("IFLA_PORT_RESPONSE=%d\n",
					*(uint16_t*)RTA_DATA(tb3[IFLA_PORT_RESPONSE]));
			}
		}
	}
}

struct nl_msg *event_if_constructResponse(struct nlmsghdr *nlh, int ifindex)
{
	struct nl_msg *nl_msg;
	struct nlattr *vf_ports = NULL, *vf_port;
	struct ifinfomsg ifinfo;
	struct vdp_data *vd;
	uint32_t pid = nlh->nlmsg_pid;
	uint32_t seq = nlh->nlmsg_seq;
	char *ifname = malloc(IFNAMSIZ);
	struct vsi_profile *p;

	nl_msg = nlmsg_alloc();

	if (!nl_msg) {
		LLDPAD_ERR("%s(%i): Unable to allocate netlink message !\n", __func__, __LINE__);
		return NULL;
	}

	if (!if_indextoname(ifindex, ifname)) {
		LLDPAD_ERR("%s(%i): No name found for interface with index %i !\n", __func__, __LINE__,
		       ifindex);
	}

	vd = vdp_data(ifname);
	if (!vd) {
		LLDPAD_ERR("%s(%i): Could not find vdp_data for %s !\n", __func__, __LINE__,
		       ifname);
		return NULL;
	}

	free(ifname);

	if (nlmsg_put(nl_msg, pid, seq, NLMSG_DONE, 0, 0) == NULL)
		goto err_exit;

	ifinfo.ifi_index = ifindex;

	if (nlmsg_append(nl_msg, &ifinfo, sizeof(ifinfo), NLMSG_ALIGNTO) < 0)
		goto err_exit;

	vf_ports = nla_nest_start(nl_msg, IFLA_VF_PORTS);

	if (!vf_ports)
		goto err_exit;

	/* loop over all existing profiles on this interface and
	 * put them into the nested IFLA_VF_PORT structure */
	LIST_FOREACH(p, &vd->profile_head, profile) {
		if (p) {
			vdp_print_profile(p);

			vf_port  = nla_nest_start(nl_msg, IFLA_VF_PORT);

			if (!vf_port)
				goto err_exit;

			if (nla_put(nl_msg, IFLA_PORT_INSTANCE_UUID, 16, p->instance) < 0)
				goto err_exit;

			if (nla_put_u32(nl_msg, IFLA_PORT_VF, PORT_SELF_VF) < 0)
				goto err_exit;

			if (p->response != VDP_RESPONSE_NO_RESPONSE) {
				if (nla_put_u16(nl_msg, IFLA_PORT_RESPONSE,
						p->response) < 0)
					goto err_exit;
			}

			nla_nest_end(nl_msg, vf_port);
		}
	}

	if (vf_ports)
		nla_nest_end(nl_msg, vf_ports);

	return nl_msg;

err_exit:
	nlmsg_free(nl_msg);

	return NULL;
}

struct nl_msg *event_if_simpleResponse(uint32_t pid, uint32_t seq, int err)
{
	struct nl_msg *nl_msg = nlmsg_alloc();
	struct nlmsgerr nlmsgerr;

	memset(&nlmsgerr, 0x0, sizeof(nlmsgerr));

	nlmsgerr.error = err;
	LLDPAD_DBG("RESPONSE error code: %d\n",err);

	if (nlmsg_put(nl_msg, pid, seq, NLMSG_ERROR, 0, 0) == NULL)
		goto err_exit;

	if (nlmsg_append(nl_msg, &nlmsgerr, sizeof(nlmsgerr), NLMSG_ALIGNTO) < 0)
		goto err_exit;

	return nl_msg;

err_exit:
	nlmsg_free(nl_msg);

	return NULL;
}

static void event_iface_receive_user_space(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct nlmsghdr *nlh, *nlh2;
	struct nl_msg *nl_msg;
	struct msghdr msg;
	struct sockaddr_nl dest_addr;
	struct iovec iov;
	int result;
	int err;
	int ifindex = 0;
	char *ifname = NULL;

	nlh = (struct nlmsghdr *)calloc(1,
					NLMSG_SPACE(MAX_PAYLOAD));
	if (!nlh) {
		LLDPAD_ERR("%s(%i): could not allocate nlh !\n", __func__,
		       __LINE__);
		return;
	}
	memset(nlh, 0, NLMSG_SPACE(MAX_PAYLOAD));

	memset(&dest_addr, 0, sizeof(dest_addr));
	iov.iov_base = (void *)nlh;
	iov.iov_len = NLMSG_SPACE(MAX_PAYLOAD);
	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	LLDPAD_DBG("Waiting for message\n");
	result = recvmsg(sock, &msg, MSG_DONTWAIT);

	LLDPAD_DBG("%s(%i): ", __func__, __LINE__);
	LLDPAD_DBG("recvmsg received %d bytes\n", result);

	if(result < 0) {
		LLDPAD_ERR("Error receiving from netlink socket : %s\n", strerror(errno));
	}

	LLDPAD_DBG("dest_addr.nl_pid: %d\n", dest_addr.nl_pid);
	LLDPAD_DBG("nlh.nl_pid: %d\n", nlh->nlmsg_pid);
	LLDPAD_DBG("nlh_type: %d\n", nlh->nlmsg_type);
	LLDPAD_DBG("nlh_seq: 0x%x\n", nlh->nlmsg_seq);
	LLDPAD_DBG("nlh_len: 0x%x\n", nlh->nlmsg_len);

	switch (nlh->nlmsg_type) {
		case RTM_SETLINK:
			LLDPAD_DBG("RTM_SETLINK\n");

			err = event_if_parse_setmsg(nlh);

			/* send simple response wether profile was accepted
			 * or not */
			nl_msg = event_if_simpleResponse(nlh->nlmsg_pid,
							 nlh->nlmsg_seq,
							 err);
			nlh2 = nlmsg_hdr(nl_msg);
			break;
		case RTM_GETLINK:
			LLDPAD_DBG("RTM_GETLINK\n");

			err = event_if_parse_getmsg(nlh, &ifindex, ifname);

			if (err) {
				nl_msg = event_if_simpleResponse(nlh->nlmsg_pid,
								 nlh->nlmsg_seq,
								 err);
			} else if (ifname) {
				ifindex = if_nametoindex(ifname);
				LLDPAD_DBG("%s(%i): ifname %s (%d)\n", __func__,
				       __LINE__, ifname, ifindex);
			} else {
				LLDPAD_DBG("%s(%i): ifindex %i\n", __func__,
				       __LINE__, ifindex);
			}

			nl_msg = event_if_constructResponse(nlh, ifindex);

			if (!nl_msg) {
				LLDPAD_ERR("%s(%i): Unable to construct response !\n",
				       __func__, __LINE__);
				goto out_err;
			}

			nlh2 = nlmsg_hdr(nl_msg);

			LLDPAD_DBG("RESPONSE:\n");

			event_if_parseResponseMsg(nlh2);

			break;
	}

	iov.iov_base = (void*)nlh2;
	iov.iov_len = nlh2->nlmsg_len;

	msg.msg_name = (void *)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	result = sendmsg(sock, &msg, 0);

	if (result < 0) {
		LLDPAD_ERR("Error sending on netlink socket (%s) !\n", strerror(errno));
	} else {
		LLDPAD_DBG("Sent %d bytes !\n",result);
	}

out_err:
	free(nlh);
	nlmsg_free(nl_msg);

	return;
}

static void event_iface_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct nlmsghdr *nlh;
	struct sockaddr_nl dest_addr;
	char buf[MAX_PAYLOAD];
	socklen_t fromlen = sizeof(dest_addr);
	int result;
	
	result = recvfrom(sock, buf, sizeof(buf), MSG_DONTWAIT,
		       (struct sockaddr *) &dest_addr, &fromlen);

	if (result < 0) {
		perror("recvfrom(Event interface)");
		eloop_register_timeout(INI_TIMER, 0, scan_port, NULL, NULL);
		return;
	}

	LLDPAD_DBG("%s:%s result from receive: %d.\n",
		   __FILE__, __FUNCTION__, result);

	/* userspace messages handled in event_iface_receive_user_space() */
	if (dest_addr.nl_pid != 0)
		return;

	nlh = (struct nlmsghdr *)buf;
	event_if_process_recvmsg(nlh);
}

int event_iface_init()
{
	int fd;
	int rcv_size = MAX_PAYLOAD;
	struct sockaddr_nl snl;

	fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	if (fd < 0)
		return fd;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv_size, sizeof(int)) < 0) {
		close(fd);
		return -EIO;
	}

	memset((void *)&snl, 0, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_groups = RTMGRP_LINK;

	if (bind(fd, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl)) < 0) {
		close(fd);
		return -EIO;
	}

	return eloop_register_read_sock(fd, event_iface_receive, NULL, NULL);
}

int event_iface_init_user_space()
{
	int fd;
	int rcv_size = MAX_PAYLOAD;
	struct sockaddr_nl snl;

	fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	if (fd < 0)
		return fd;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv_size, sizeof(int)) < 0) {
		close(fd);
		return -EIO;
	}

	memset((void *)&snl, 0, sizeof(struct sockaddr_nl));
	snl.nl_family = AF_NETLINK;
	snl.nl_pid = getpid();  /* self pid */
	snl.nl_groups = 0;

	if (bind(fd, (struct sockaddr *)&snl, sizeof(struct sockaddr_nl)) < 0) {
		close(fd);
		LLDPAD_ERR("Error binding to netlink socket (%s) !\n", strerror(errno));
		return -EIO;
	}

	return eloop_register_read_sock(fd, event_iface_receive_user_space,
					NULL, NULL);
}

int event_iface_deinit()
{
	return 0;
}
