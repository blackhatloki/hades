/*******************************************************************************

  implementation of EVB TLVs for LLDP
  (c) Copyright IBM Corp. 2010

  Author(s): Jens Osterkamp <jens at linux.vnet.ibm.com>

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

*******************************************************************************/

#ifndef _LLDP_EVB_H
#define _LLDP_EVB_H

#include "lldp_mod.h"

#define LLDP_MOD_EVB	OUI_IEEE_8021Qbg
#define LLDP_OUI_SUBTYPE	{ 0x00, 0x1b, 0x3f, 0x00 }

typedef enum {
	EVB_OFFER_CAPABILITIES = 0,
	EVB_CONFIGURE,
	EVB_CONFIRMATION
} evb_state;

#define	EVB_RTE		13
/* retransmission granularity (RTG) in microseconds */
#define EVB_RTG		10
/* retransmission multiplier (RTM) */
#define EVB_RTM(rte)	(2<<(rte-1))

struct tlv_info_evb {
	u8 oui[3];
	u8 sub;
	/* supported forwarding mode */
	u8 smode;
	/* supported capabilities */
	u8 scap;
	/* currently configured forwarding mode */
	u8 cmode;
	/* currently configured capabilities */
	u8 ccap;
	/* supported no. of vsi */
	u16 svsi;
	/* currently configured no. of vsi */
	u16 cvsi;
	/* retransmission exponent */
	u8 rte;
} __attribute__ ((__packed__));

struct evb_data {
	char ifname[IFNAMSIZ];
	struct unpacked_tlv *evb;
	struct tlv_info_evb *tie;
	/* local policy */
	struct tlv_info_evb *policy;
	int state;
	LIST_ENTRY(evb_data) entry;
};

struct evb_user_data {
	LIST_HEAD(evb_head, evb_data) head;
};

struct lldp_module *evb_register(void);
void evb_unregister(struct lldp_module *mod);
struct packed_tlv *evb_gettlv(struct port *port);
void evb_ifdown(char *);
void evb_ifup(char *);
struct evb_data *evb_data(char *ifname);

#endif /* _LLDP_EVB_H */
