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

#ifndef _LLDPTOOL_H
#define _LLDPTOOL_H

#include "clif.h"
#include "clif_msgs.h"

struct lldp_head lldp_cli_head;

int hex2int(char *b);
int clif_command(struct clif *clif, char *cmd, int raw);
void print_raw_message(char *msg, int print);
int parse_print_message(char *msg, int print);
void print_event_msg(char *buf);
void print_response(char *buf, int status);
int parse_response(char *buf);
/*
void print_dcb_cmd_response(char *buf, cmd_status status);
int handle_dcb_cmds(struct clif *clif, int argc, char *argv[], int raw);
*/

#endif /* _LLDPTOOL_H */
