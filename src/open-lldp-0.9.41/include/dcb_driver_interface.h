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

#ifndef _DCB_DRIVER_INTERFACE_H_
#define _DCB_DRIVER_INTERFACE_H_

typedef struct pgroup_attribs {
	dcb_traffic_attribs_type tx;
	dcb_traffic_attribs_type rx;
} pgroup_attribs;

#ifdef DCB_APP_DRV_IF_SUPPORTED
typedef struct appgroup_attribs {
	u8  dcb_app_idtype;
	u16 dcb_app_id;
	u8  dcb_app_priority;
} appgroup_attribs;
#endif

int set_hw_pg(char *device_name, pgroup_attribs *pg_data, bool Opermode);
int set_hw_pfc(char *device_name, dcb_pfc_list_type pfc_data, bool Opermode);

#ifdef DCB_APP_DRV_IF_SUPPORTED
int set_hw_app0(char *device_name, appgroup_attribs *app_data);
int set_hw_app1(char *device_name, u8 priority, int mode);
#endif /* DCB_APP_DRV_IF_SUPPORTED */

int set_hw_all(char *device_name);

int get_dcb_capabilities(char *device_name, struct feature_support *dcb_capabilites);
u32 double_to_fixpt_int(double double_val);

int get_dcb_numtcs(char *device_name, u8 *pgtcs, u8 *pfctcs);
#endif /* _DCB_DRIVER_INTERFACE_H_ */

