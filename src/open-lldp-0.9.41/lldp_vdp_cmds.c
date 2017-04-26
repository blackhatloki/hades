/******************************************************************************

  implementation of VDP according to IEEE 802.1Qbg
  (c) Copyright IBM Corp. 2010

  Author(s): Jens Osterkamp <jens@linux.vnet.ibm.com>

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

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "lldpad.h"
#include "ctrl_iface.h"
#include "lldp.h"
#include "lldp_vdp.h"
#include "lldp_mand_clif.h"
#include "lldp_vdp_clif.h"
#include "lldp_vdp_cmds.h"
#include "lldp/ports.h"
#include "messages.h"
#include "libconfig.h"
#include "config.h"
#include "clif_msgs.h"
#include "lldp/states.h"

static int get_arg_tlvtxenable(struct cmd *, char *, char *, char *);
static int set_arg_tlvtxenable(struct cmd *, char *, char *, char *);

static int get_arg_mode(struct cmd *, char *, char *, char *);
static int set_arg_mode(struct cmd *, char *, char *, char *);

static int get_arg_role(struct cmd *, char *, char *, char *);
static int set_arg_role(struct cmd *, char *, char *, char *);

static struct arg_handlers arg_handlers[] = {
       { ARG_VDP_MODE, get_arg_mode, set_arg_mode },
       { ARG_VDP_ROLE, get_arg_role, set_arg_role },
       { ARG_TLVTXENABLE, get_arg_tlvtxenable, set_arg_tlvtxenable },
       { NULL }
};

static int get_arg_tlvtxenable(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       int value;
       char *s;
       char arg_path[VDP_BUF_SIZE];

       if (cmd->cmd != cmd_gettlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               snprintf(arg_path, sizeof(arg_path), "%s.%s",
                        VDP_PREFIX, arg);

               if (get_cfg(cmd->ifname, arg_path, (void *)&value,
                                       CONFIG_TYPE_BOOL))
                       value = false;
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       if (value)
               s = VAL_YES;
       else
               s = VAL_NO;

       sprintf(obuf, "%02x%s%04x%s", (unsigned int) strlen(arg), arg,
               (unsigned int) strlen(s), s);

       return cmd_success;
}

static int set_arg_tlvtxenable(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       int value;
       char arg_path[VDP_BUF_SIZE];

       if (cmd->cmd != cmd_settlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       if (!strcasecmp(argvalue, VAL_YES))
               value = 1;
       else if (!strcasecmp(argvalue, VAL_NO))
               value = 0;
       else
               return cmd_invalid;

       snprintf(arg_path, sizeof(arg_path), "%s.%s", VDP_PREFIX, arg);

       if (set_cfg(cmd->ifname, arg_path, (void *)&value, CONFIG_TYPE_BOOL))
               return cmd_failed;

       return cmd_success;
}

static int get_arg_mode(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       char *s, *t;
       struct vsi_profile *np;
       struct vdp_data *vd;
       int count=0;

       if (cmd->cmd != cmd_gettlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       vd = vdp_data(cmd->ifname);
       if (!vd) {
               LLDPAD_ERR("%s(%i): vdp_data for %s not found !\n", __func__, __LINE__,
                      cmd->ifname);
               free(t);
               return cmd_invalid;
       }

       LIST_FOREACH(np, &vd->profile_head, profile) {
               count++;
       }

       s = t = malloc(count*VDP_BUF_SIZE+1);
       if (!s)
               return cmd_invalid;
       memset(s, 0, count*VDP_BUF_SIZE+1);

       LIST_FOREACH(np, &vd->profile_head, profile) {
               PRINT_PROFILE(t, np);
       }

       sprintf(obuf, "%02x%s%04x%s", (unsigned int) strlen(arg), arg,
               (unsigned int) strlen(s), s);

       free(s);

       return cmd_success;
}

static void str2instance(struct vsi_profile *profile, char *buffer)
{
       int i, j = 0;
       char instance[INSTANCE_STRLEN+2];

       for(i=0; i <= strlen(buffer); i++) {
               if (buffer[i] == '-') {
                       continue;
               }

               if ((sscanf(&buffer[i], "%02x", &profile->instance[j]) == 1) ||
                   (sscanf(&buffer[i], "%02X", &profile->instance[j]) == 1)) {
                       i++;
                       j++;
               }
       }
}

/* INSTANCE_STRLEN = strlen("fa9b7fff-b0a0-4893-abcd-beef4ff18f8f") */
#define INSTANCE_STRLEN 36

int instance2str(const u8 *p, char *dst, size_t size)
{
       if (dst && size > INSTANCE_STRLEN) {
               snprintf(dst, size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                        p[0], p[1], p[2], p[3],
                        p[4], p[5], p[6], p[7],
                        p[8], p[9], p[10], p[11],
                        p[12], p[13], p[14], p[15]);
               return 0;
       }
       return -1;
}

static void vdp_fill_profile(struct vsi_profile *profile, char *buffer, int field)
{
       switch(field) {
               case MODE:
                       profile->mode = atoi(buffer);
                       break;
               case MGRID:
                       profile->mgrid = atoi(buffer);
                       break;
               case TYPEID:
                       profile->id = atoi(buffer);
                       break;
               case TYPEIDVERSION:
                       profile->version = atoi(buffer);
                       break;
               case INSTANCEID:
                       str2instance(profile, buffer);
                       break;
               case MAC:
                       str2mac(buffer, &profile->mac[0], MAC_ADDR_LEN);
                       break;
               case VLAN:
                       profile->vlan = atoi(buffer);
                       break;
               default:
                       LLDPAD_ERR("Unknown field in buffer !\n");
                       break;
       }
}

static struct vsi_profile *vdp_parse_mode_line(char * argvalue)
{
       int i, arglen, field;
       char *cmdstring, *buf;
       char *buffer;
       struct vsi_profile *profile;

       profile = malloc(sizeof(struct vsi_profile));
       if (!profile)
               return NULL;
       memset(profile, 0, sizeof(struct vsi_profile));

       arglen = strlen(argvalue);
       cmdstring = argvalue;
       buffer = malloc(arglen);
       if (!buffer)
               goto out_free;
       buf = buffer;
       field = 0;

       for (i=0; i <= arglen; i++) {
               *buffer = *cmdstring;

               if ((*cmdstring == ',') || (*cmdstring == '\0')) {
                       *buffer++ = '\0';
                       vdp_fill_profile(profile, buf, field);
                       field++;
                       buffer = buf;
                       memset(buffer, 0, arglen);
                       cmdstring++;
                       continue;
               }

               buffer++;
               cmdstring++;
       }

       free(buffer);

       return profile;

out_free:
       free(profile);
       return NULL;
}

static int set_arg_mode(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       int arglen;
       struct vsi_profile *profile, *p;

       arglen = strlen(argvalue);

       if (cmd->cmd != cmd_settlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       profile = vdp_parse_mode_line(argvalue);
       profile->port = port_find_by_name(cmd->ifname);

       if (!profile->port) {
               free(profile);
               return cmd_invalid;
       }

       p = vdp_add_profile(profile);

       if (!p) {
               free(profile);
               return cmd_invalid;
       }

       vdp_somethingChangedLocal(profile, VDP_PROFILE_REQ);
       vdp_vsi_sm_station(p);

       return cmd_success;
}

static int get_arg_role(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       char *p;
       char arg_path[VDP_BUF_SIZE];

       if (cmd->cmd != cmd_gettlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               snprintf(arg_path, sizeof(arg_path), "%s.%s",
                        VDP_PREFIX, arg);

               if (get_cfg(cmd->ifname, arg_path, (void *)&p,
                                       CONFIG_TYPE_STRING))
                       return cmd_failed;
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       sprintf(obuf, "%02x%s%04x%s", (unsigned int) strlen(arg), arg,
               (unsigned int) strlen(p), p);

       return cmd_success;
}

static int set_arg_role(struct cmd *cmd, char *arg, char *argvalue,
                              char *obuf)
{
       struct vdp_data *vd;
       char arg_path[VDP_BUF_SIZE];

       if (cmd->cmd != cmd_settlv)
               return cmd_invalid;

       switch (cmd->tlvid) {
       case ((LLDP_MOD_VDP) << 8) | LLDP_VDP_SUBTYPE:
               break;
       case INVALID_TLVID:
               return cmd_invalid;
       default:
               return cmd_not_applicable;
       }

       vd = vdp_data(cmd->ifname);

       if (!vd) {
               LLDPAD_ERR("%s(%i): could not find vdp_data for %s !\n",
                      __FILE__, __LINE__, cmd->ifname);
               return cmd_invalid;
       }

       if (!strcasecmp(argvalue, VAL_BRIDGE)) {
               vd->role = VDP_ROLE_BRIDGE;
       } else if (!strcasecmp(argvalue, VAL_STATION)) {
               vd->role = VDP_ROLE_STATION;
       } else {
               return cmd_invalid;
       }

       snprintf(arg_path, sizeof(arg_path), "%s.%s", VDP_PREFIX, arg);

       char *p = &argvalue[0];
       if (set_cfg(cmd->ifname, arg_path, (void *)&p, CONFIG_TYPE_STRING))
               return cmd_failed;

       return cmd_success;
}


struct arg_handlers *vdp_get_arg_handlers()
{
       return &arg_handlers[0];
}
