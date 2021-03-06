/*******************************************************************************

  implementation of ECP according to 802.1Qbg
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

#include <stdio.h>
#include <assert.h>
#include "lldp/ports.h"
#include "lldp/l2_packet.h"
#include "eloop.h"
#include "messages.h"
#include "lldpad.h"
#include "lldp_tlv.h"
#include "lldp_mod.h"
#include "lldp_mand.h"
#include "lldp_evb.h"
#include "include/lldp_vdp.h"

void ecp_tx_run_sm(struct vdp_data *);

/* ecp_somethingChangedLocal - set flag if port has changed
 * @vd: port to set the flag for
 * @mode: mode to set the flag to
 *
 * no return value
 *
 * set the localChange flag with a mode to indicate a port has changed.
 * used  to signal an ecpdu needs to be sent out.
 */

void ecp_somethingChangedLocal(struct vdp_data *vd)
{
	if (!vd)
		return;

	vd->ecp.tx.localChange = 1;

	return;
}

/* ecp_print_frameout - print outbound frame
 * @vd: currently used port
 *
 * no return value
 *
 * prints a raw dump of an outbound ecp frame. useful for low-level protocol
 * debugging.
 */
void ecp_print_frameout(struct vdp_data *vd)
{
	int i;
	char *s, *t;

	s = t = malloc(256);

	 if (!s) {
		LLDPAD_ERR("%s(%i): unable to allocate string !\n", __func__, __LINE__);
	 }

	for (i=0; i < vd->ecp.tx.sizeout; i++) {
		int c;
		c = sprintf(s, "%02x ", vd->ecp.tx.frameout[i]);
		s += c;
		if (!((i+1) % 16)) {
			LLDPAD_DBG("%s\n", t);
			s = t;
		}
	}

	LLDPAD_DBG("%s\n", t);

	free(t);
}

/* ecp_build_ECPDU - create an ecp protocol data unit
 * @vd: currently used port
 *
 * returns true on success, false on failure
 *
 * creates the frame header with the ports mac address, the ecp header with REQ
 * plus a list of packed TLVs created from the profiles on this
 * port.
 */
bool ecp_build_ECPDU(struct vdp_data *vd)
{
	struct l2_ethhdr eth;
	struct ecp_hdr ecp_hdr;
	u8  own_addr[ETH_ALEN];
	u32 fb_offset = 0;
	u32 datasize = 0;
	struct packed_tlv *ptlv =  NULL;
	struct vsi_profile *p;

	/* TODO: different multicast address for sending ECP over S-channel (multi_cast_source_s)
	 * S-channels to implement later */
	memcpy(eth.h_dest, multi_cast_source, ETH_ALEN);
	l2_packet_get_own_src_addr(vd->ecp.l2,(u8 *)&own_addr);
	memcpy(eth.h_source, &own_addr, ETH_ALEN);
	eth.h_proto = htons(ETH_P_ECP);
	vd->ecp.tx.frameout = (u8 *)malloc(ETH_FRAME_LEN);
	if (vd->ecp.tx.frameout == NULL) {
		LLDPAD_ERR("InfoECPDU: Failed to malloc frame buffer \n");
		return false;
	}
	memset(vd->ecp.tx.frameout,0,ETH_FRAME_LEN);
	memcpy(vd->ecp.tx.frameout, (void *)&eth, sizeof(struct l2_ethhdr));
	fb_offset += sizeof(struct l2_ethhdr);

	ecp_hdr.oui[0] = 0x0;
	ecp_hdr.oui[1] = 0x1b;
	ecp_hdr.oui[2] = 0x3f;

	ecp_hdr.pad1 = 0x0;

	ecp_hdr.subtype = ECP_SUBTYPE;
	ecp_hdr.mode = ECP_REQUEST;

	vd->ecp.lastSequence++;
	ecp_hdr.seqnr = htons(vd->ecp.lastSequence);

	if ((sizeof(struct ecp_hdr)+fb_offset) > ETH_MAX_DATA_LEN)
				goto error;
	memcpy(vd->ecp.tx.frameout+fb_offset, (void *)&ecp_hdr, sizeof(struct ecp_hdr));
	datasize += sizeof(struct ecp_hdr);
	fb_offset += sizeof(struct ecp_hdr);

	/* create packed_tlvs for all profiles on this interface */
	LIST_FOREACH(p, &vd->profile_head, profile) {
		if(!p) {
			LLDPAD_ERR("%s(%i): list vd->profile_head empty !\n", __func__, __LINE__);
			continue;
		}

		if (!p->localChange) {
			LLDPAD_DBG("%s(%i): skipping unchanged profile !\n", __func__, __LINE__);
			continue;
		}

		ptlv = vdp_gettlv(vd, p);

		if (!ptlv) {
			LLDPAD_ERR("%s(%i): ptlv not created !\n", __func__, __LINE__);
			continue;
		}

		if (ptlv) {
			if ((ptlv->size+fb_offset) > ETH_MAX_DATA_LEN)
				goto error;
			memcpy(vd->ecp.tx.frameout+fb_offset,
			       ptlv->tlv, ptlv->size);
			datasize += ptlv->size;
			fb_offset += ptlv->size;
		}

		ptlv = free_pkd_tlv(ptlv);
	}

	/* The End TLV marks the end of the LLDP PDU */
	ptlv = pack_end_tlv();
	if (!ptlv || ((ptlv->size + fb_offset) > ETH_MAX_DATA_LEN))
		goto error;
	memcpy(vd->ecp.tx.frameout + fb_offset, ptlv->tlv, ptlv->size);
	datasize += ptlv->size;
	fb_offset += ptlv->size;
	ptlv =  free_pkd_tlv(ptlv);

	if (datasize > ETH_MAX_DATA_LEN)
		goto error;

	if (datasize < ETH_MIN_DATA_LEN)
		vd->ecp.tx.sizeout = ETH_MIN_PKT_LEN;
	else
		vd->ecp.tx.sizeout = fb_offset;

	return true;

error:
	ptlv = free_pkd_tlv(ptlv);
	if (vd->ecp.tx.frameout)
		free(vd->ecp.tx.frameout);
	vd->ecp.tx.frameout = NULL;
	LLDPAD_ERR("InfoECPDU: packed TLV too large for tx frame\n");
	return false;
}

/* ecp_tx_Initialize - initializes the ecp tx state machine
 * @vd: currently used port
 *
 * no return value
 *
 * initializes some variables for the ecp tx state machine.
 */
void ecp_tx_Initialize(struct vdp_data *vd)
{
	if (vd->ecp.tx.frameout) {
		free(vd->ecp.tx.frameout);
		vd->ecp.tx.frameout = NULL;
	}
	vd->ecp.tx.localChange = VDP_PROFILE_REQ;
	vd->ecp.lastSequence = ECP_SEQUENCE_NR_START;
	vd->ecp.stats.statsFramesOutTotal = 0;
	vd->ecp.ackTimerExpired = false;
	vd->ecp.retries = 0;

	struct port *port = port_find_by_name(vd->ifname);
	l2_packet_get_port_state(vd->ecp.l2, (u8 *)&(port->portEnabled));

	return;
}

/* ecp_txFrame - transmit ecp frame
 * @vd: currently used port
 *
 * returns the number of characters sent on success, -1 on failure
 *
 * sends out the frame stored in the frameout structure using l2_packet_send.
 */
u8 ecp_txFrame(struct vdp_data *vd)
{
	int status = 0;

	status = l2_packet_send(vd->ecp.l2, (u8 *)&multi_cast_source,
		htons(ETH_P_ECP),vd->ecp.tx.frameout,vd->ecp.tx.sizeout);
	vd->ecp.stats.statsFramesOutTotal++;

	free(vd->ecp.tx.frameout);

	return status;
}

/* ecp_tx_create_frame - create ecp frame
 * @vd: currently used port
 *
 * no return value
 *
 *
 */
void ecp_tx_create_frame(struct vdp_data *vd)
{
	/* send REQs */
	if (vd->ecp.tx.localChange) {
		LLDPAD_DBG("%s(%i)-%s: sending REQs\n", __func__, __LINE__, vd->ifname);
		ecp_build_ECPDU(vd);
		ecp_print_frameout(vd);
		ecp_txFrame(vd);
	}

	vd->ecp.tx.localChange = 0;
	return;
}

/* ecp_timeout_handler - handles the ack timer expiry
 * @eloop_data: data structure of event loop
 * @user_ctx: user context, port here
 *
 * no return value
 *
 * called when the ECP ack timer has expired. sets a flag and calls the ECP
 * state machine.
 */
static void ecp_tx_timeout_handler(void *eloop_data, void *user_ctx)
{
	struct vdp_data *vd;

	vd = (struct vdp_data *) user_ctx;

	vd->ecp.ackTimerExpired = true;

	LLDPAD_DBG("%s(%i)-%s: timer expired\n", __func__, __LINE__,
	       vd->ifname);

	ecp_tx_run_sm(vd);
}

/* ecp_tx_stop_ackTimer - stop the ECP ack timer
 * @vd: currently used port
 *
 * returns the number of removed handlers
 *
 * stops the ECP ack timer. used when a ack frame for the port has been
 * received.
 */
int ecp_tx_stop_ackTimer(struct vdp_data *vd)
{
	LLDPAD_DBG("%s(%i)-%s: stopping timer\n", __func__, __LINE__,
	       vd->ifname);

	return eloop_cancel_timeout(ecp_tx_timeout_handler, NULL, (void *) vd);
}

/* ecp_tx_start_ackTimer - starts the ECP ack timer
 * @profile: profile to process
 *
 * returns 0 on success, -1 on error
 *
 * starts the ack timer when a frame has been sent out.
 */
static void ecp_tx_start_ackTimer(struct vdp_data *vd)
{
	unsigned int secs, usecs, rte;

	vd->ecp.ackTimerExpired = false;

	rte = evb_get_rte(vd->ifname);

	secs = ECP_TRANSMISSION_TIMER(rte) / ECP_TRANSMISSION_DIVIDER;
	usecs = ECP_TRANSMISSION_TIMER(rte) % ECP_TRANSMISSION_DIVIDER;

	LLDPAD_DBG("%s(%i)-%s: starting timer\n", __func__, __LINE__,
	       vd->ifname);

	eloop_register_timeout(secs, usecs, ecp_tx_timeout_handler, NULL, (void *) vd);
}

/* ecp_tx_change_state - changes the ecp tx sm state
 * @vd: currently used port
 * @newstate: new state for the sm
 *
 * no return value
 *
 * checks state transistion for consistency and finally changes the state of
 * the profile.
 */
static void ecp_tx_change_state(struct vdp_data *vd, u8 newstate)
{
	switch(newstate) {
	case ECP_TX_INIT_TRANSMIT:
		break;
	case ECP_TX_TRANSMIT_ECPDU:
		assert((vd->ecp.tx.state == ECP_TX_INIT_TRANSMIT) ||
		       (vd->ecp.tx.state == ECP_TX_WAIT_FOR_ACK) ||
		       (vd->ecp.tx.state == ECP_TX_REQUEST_PDU));
		break;
	case ECP_TX_WAIT_FOR_ACK:
		assert(vd->ecp.tx.state == ECP_TX_TRANSMIT_ECPDU);
		break;
	case ECP_TX_REQUEST_PDU:
		assert(vd->ecp.tx.state == ECP_TX_WAIT_FOR_ACK);
		break;
	default:
		LLDPAD_ERR("ERROR: The ECP_TX State Machine is broken!\n");
		log_message(MSG_ERR_TX_SM_INVALID, "%s", vd->ifname);
	}

	LLDPAD_DBG("%s(%i)-%s: state change %s -> %s\n", __func__, __LINE__,
	       vd->ifname, ecp_tx_states[vd->ecp.tx.state], ecp_tx_states[newstate]);

	vd->ecp.tx.state = newstate;
	return;
}

/* ecp_set_tx_state - sets the ecp tx sm state
 * @vd: currently used port
 *
 * returns true or false
 *
 * switches the state machine to the next state depending on the input
 * variables. returns true or false depending on wether the state machine
 * can be run again with the new state or can stop at the current state.
 */
static bool ecp_set_tx_state(struct vdp_data *vd)
{
	struct port *port = port_find_by_name(vd->ifname);

	if (!port) {
		LLDPAD_ERR("%s(%i): port not found !\n", __func__, __LINE__);
		return 0;
	}

	if ((port->portEnabled == false) && (port->prevPortEnabled == true)) {
		LLDPAD_ERR("set_tx_state: port was disabled\n");
		ecp_tx_change_state(vd, ECP_TX_INIT_TRANSMIT);
	}
	port->prevPortEnabled = port->portEnabled;

	switch (vd->ecp.tx.state) {
	case ECP_TX_INIT_TRANSMIT:
		if (port->portEnabled && ((port->adminStatus == enabledRxTx) ||
			(port->adminStatus == enabledTxOnly)) && vd->ecp.tx.localChange) {
			ecp_tx_change_state(vd, ECP_TX_TRANSMIT_ECPDU);
			return true;
		}
		return false;
	case ECP_TX_TRANSMIT_ECPDU:
		if ((port->adminStatus == disabled) ||
			(port->adminStatus == enabledRxOnly)) {
			ecp_tx_change_state(vd, ECP_TX_INIT_TRANSMIT);
			return true;
		}
		ecp_tx_change_state(vd, ECP_TX_WAIT_FOR_ACK);
		return true;
	case ECP_TX_WAIT_FOR_ACK:
		if (vd->ecp.ackTimerExpired) {
			vd->ecp.retries++;
			if (vd->ecp.retries < ECP_MAX_RETRIES) {
				ecp_somethingChangedLocal(vd);
				ecp_tx_change_state(vd, ECP_TX_TRANSMIT_ECPDU);
				return true;
			}
			if (vd->ecp.retries == ECP_MAX_RETRIES) {
				LLDPAD_DBG("%s(%i)-%s: 1 \n", __func__, __LINE__,
				       vd->ifname);
				ecp_tx_change_state(vd, ECP_TX_REQUEST_PDU);
				return true;
			}
		}
		if (vd->ecp.ackReceived && vd->ecp.seqECPDU == vd->ecp.lastSequence) {
			vd->ecp.ackReceived = false;
			ecp_tx_change_state(vd, ECP_TX_REQUEST_PDU);
			return true;
		}
		return false;
	case ECP_TX_REQUEST_PDU:
		if (vd->ecp.tx.localChange & VDP_PROFILE_REQ) {
			ecp_tx_change_state(vd, ECP_TX_TRANSMIT_ECPDU);
			return true;
		}
		return false;
	default:
		LLDPAD_ERR("ERROR: The TX State Machine is broken!\n");
		log_message(MSG_ERR_TX_SM_INVALID, "%s", vd->ifname);
		return false;
	}
}

/* ecp_tx_run_sm - state machine for ecp tx
 * @vd: currently used vdp_data
 *
 * no return value
 *
 * runs the state machine for ecp tx.
 */
void ecp_tx_run_sm(struct vdp_data *vd)
{
	do {
		LLDPAD_DBG("%s(%i)-%s: ecp_tx - %s\n", __func__, __LINE__,
		       vd->ifname, ecp_tx_states[vd->ecp.tx.state]);

		switch(vd->ecp.tx.state) {
		case ECP_TX_INIT_TRANSMIT:
			ecp_tx_Initialize(vd);
			break;
		case ECP_TX_TRANSMIT_ECPDU:
			ecp_tx_create_frame(vd);
			ecp_tx_start_ackTimer(vd);
			break;
		case ECP_TX_WAIT_FOR_ACK:
			if (vd->ecp.ackReceived) {
				LLDPAD_DBG("%s(%i)-%s: ECP_TX_WAIT_FOR_ACK ackReceived\n", __func__, __LINE__,
				       vd->ifname);
				LLDPAD_DBG("%s(%i)-%s: seqECPDU %x lastSequence %x \n", __func__, __LINE__,
				       vd->ifname, vd->ecp.seqECPDU, vd->ecp.lastSequence);
				vd->ecp.tx.localChange = 0;
				ecp_tx_stop_ackTimer(vd);
			}
			break;
		case ECP_TX_REQUEST_PDU:
			vd->ecp.retries = 0;
			LLDPAD_DBG("%s(%i)-%s: ECP_TX_REQUEST_PDU lastSequence %x\n", __func__, __LINE__,
			       vd->ifname, vd->ecp.lastSequence);
			break;
		default:
			LLDPAD_ERR("%s(%i): ERROR The TX State Machine is broken!\n", __func__,
			       __LINE__);
			log_message(MSG_ERR_TX_SM_INVALID, "%s", vd->ifname);
		}
	} while (ecp_set_tx_state(vd) == true);

	return;
}
