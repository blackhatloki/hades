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

#ifndef _LLDP_MOD_H
#define _LLDP_MOD_H

#include <sys/queue.h>
#include <sys/un.h>
#include "lldp_util.h"
#include "ctrl_iface.h"

/* 
 * Ops structure for lldp module callbacks.
 *
 * @lldp_mod_register: return lldp_module struct with tlv or out+subtype 
 * 		       match types
 * @lldp_mod_unregister: cleanup
 * @lldp_mod_gettlv: return packed_tlv for core to append and xmit,
 * 		     module is responsible for sanity checks the core
 * 		     will only verify length.
 * @lldp_mod_rchange: core recv function passing changed tlv to module
 * @lldp_mod_utlv: update tlv called before each xmit
 * @lldp_mod_ifup: notification of rtnetlink LINK_UP event 
 * @lldp_mod_ifdown: notification of rtnetlink LINK_DOWN event 
 * @lldp_mod_recvrt: core passes raw rtnetlink messages 
 * @client_register: any setup required for client interface
 * @client_cmd: process client commands from core lldp
 * @print_tlv: routine for client to pretty print TLVs
 * @lookup_tlv_name: find tlvid given a tlv 'name'
 * @get_arg_handler: return an arg handler list
 */
struct lldp_mod_ops {
	struct lldp_module * 	(* lldp_mod_register)(void);
	void 			(* lldp_mod_unregister)(struct lldp_module *);
	struct packed_tlv * 	(* lldp_mod_gettlv)(struct port *);
	int  			(* lldp_mod_rchange)(struct port *,
						    struct unpacked_tlv *);
	void  			(* lldp_mod_utlv)(struct port *);
	void  			(* lldp_mod_ifup)(char *); 
	void			(* lldp_mod_ifdown)(char *);
	u8 			(* lldp_mod_mibdelete)(struct port *port);
	u32			(* client_register)(void);
	int  			(* client_cmd)(void *data,
					      struct sockaddr_un *from,
					      socklen_t fromlen, char *ibuf,
					      int ilen, char *rbuf);
	int  			(* print_tlv)(u32, u16, char *);
	u32			(* lookup_tlv_name)(char *);
	int			(* print_help)();
	int			(* timer)();
	struct arg_handlers *	(* get_arg_handler)(void);
};

/*
 *	The lldp module structure
 *
 *	lldp module per instance structure.  Used by lldp core to 
 *	track available modules.  Expect lldp core to create link
 *	list of modules types per port.
 *
 */
struct lldp_module {
	int id;		/* match tlv or oui+subtype */
	u8 enable;	/* TX only, RX only, TX+RX, Disabled */
	char *path; 	/* shared library path */
	void *dlhandle; /* dlopen handle for closing */
	void *data;	/* module specific data */
	const struct lldp_mod_ops *ops;
	LIST_ENTRY(lldp_module) lldp;
};

LIST_HEAD(lldp_head, lldp_module);
struct lldp_head lldp_head;


static inline struct lldp_module *find_module_by_id(struct lldp_head *head, int id)
{
 	struct lldp_module *mod;

	LIST_FOREACH(mod, head, lldp) {
		if (mod->id == id)
			return mod;
	}
	return NULL;
}

static inline void *find_module_user_data_by_if(const char *ifname,
						struct lldp_head *head, int id)
{
	struct lldp_module *mod;

	mod = find_module_by_id(head, id);
	if (mod)
		return mod->data;
	return NULL;
}
#endif /* _LLDP_MOD_H */
