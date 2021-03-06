# target programs to be installed in ${sbindir}
sbin_PROGRAMS = lldpad dcbtool lldptool

# package nltest, but do not install it anywhere
noinst_PROGRAMS = nltest

# look for header files in the include directory
# pass the value of ${sysconfdir} to the C pre-processor as SYSCONFDIR
AM_CPPFLAGS = -I${srcdir}/include -DSYSCONFDIR="\"${sysconfdir}\""

## system requires a shared libconfig
AM_CFLAGS = $(LIBCONFIG_CFLAGS)
AM_LDFLAGS = $(LIBCONFIG_LIBS)

## header files to be installed, for programs using the client interface to lldpad 
lldpad_includedir= ${includedir}/lldpad

lldpad_include_HEADERS = include/dcb_types.h \
include/clif.h include/lldp_dcbx_cmds.h \
include/lldpad.h include/lldp_mand_cmds.h \
include/clif_msgs.h include/lldp_basman_cmds.h include/lldp_8023_cmds.h \
include/lldp_med_cmds.h include/lldp_util.h \
include/lldp_evb_cmds.h include/lldp_vdp_cmds.h

noinst_HEADERS = include/config.h include/ctrl_iface.h \
include/dcb_driver_interface.h \
include/dcb_events.h include/dcb_persist_store.h include/dcb_protocol.h \
include/dcb_rule_chk.h include/lldp_dcbx_nl.h include/eloop.h \
include/lldpad_shm.h include/event_iface.h include/messages.h \
include/parse_cli.h include/version.h include/lldptool_cli.h include/list.h \
include/lldp_mand_clif.h include/lldp_basman_clif.h include/lldp_med_clif.h \
include/lldp_8023_clif.h include/lldp_dcbx_clif.h \
include/lldp_evb_clif.h include/lldp_vdp_clif.h \
include/lldptool.h include/lldp_rtnl.h include/dcbtool.h include/lldp_dcbx_cfg.h

lldpad_SOURCES = lldpad.c config.c lldp_dcbx_nl.c ctrl_iface.c \
event_iface.c eloop.c lldp_dcbx_cmds.c log.c lldpad_shm.c \
dcb_protocol.c dcb_rule_chk.c  list.c lldp_rtnl.c \
$(lldpad_include_HEADERS) $(noinst_HEADERS) \
lldp/ports.c lldp/agent.c lldp/l2_packet_linux.c lldp/tx.c \
lldp/rx.c lldp/agent.h lldp/l2_packet.h lldp/mibdata.h lldp/ports.h \
lldp/states.h include/lldp.h include/lldp_mod.h \
lldp_dcbx.c include/lldp_dcbx.h tlv_dcbx.c include/tlv_dcbx.h \
lldp_dcbx_cfg.c include/lldp_dcbx_cfg.h lldp_util.c \
lldp_mand.c include/lldp_mand.h \
lldp_mand_cmds.c lldp_basman_cmds.c lldp_8023_cmds.c lldp_med_cmds.c \
lldp_evb_cmds.c lldp_evb.c include/lldp_evb.h lldp_vdp_cmds.c include/lldp_vdp_cmds.h \
ecp/ecp.c ecp/ecp_tx.c ecp/ecp_rx.c ecp/ecp.h lldp_vdp.c include/lldp_vdp.h \
lldp_tlv.c include/lldp_tlv.h \
lldp_basman.c include/lldp_basman.h \
lldp_med.c include/lldp_med.h \
lldp_8023.c include/lldp_8023.h

dcbtool_SOURCES = dcbtool.c clif.c dcbtool_cmds.c parse_cli.l \
$(lldpad_include_HEADERS) $(noinst_HEADERS)

lldptool_SOURCES = lldptool.c clif.c lldptool_cmds.c lldp_rtnl.c \
		   lldp_mand_clif.c lldp_basman_clif.c lldp_med_clif.c \
		   lldp_8023_clif.c lldp_dcbx_clif.c lldp_util.c \
		   lldp_evb_clif.c lldp_vdp_clif.c \
		   $(lldpad_include_HEADERS) $(noinst_HEADERS)

nltest_SOURCES = nltest.c nltest.h

## man pages
dist_man_MANS = lldpad.8 dcbtool.8 lldptool.8

## force the creation of an empty configuration directory at install time
lldpadconfigdir = /var/lib/lldpad
lldpadconfig_DATA =

## pkg-config data file, for client interface software to find headers
pkgconfigdir = ${libdir}/pkgconfig
pkgconfig_DATA = lldpad.pc

## put a spec file and documentation in the distribution archive
dist_noinst_DATA = lldpad.spec README COPYING ChangeLog

## lldpad.init is listed here because it gets installed from install-data-local
dist_noinst_SCRIPTS = lldpad.init

## special hooks to handle the init script
install-data-local: lldpad.init
	$(MKDIR_P) $(DESTDIR)/etc/init.d
	$(INSTALL_SCRIPT) lldpad.init $(DESTDIR)/etc/init.d/lldpad

install-data-hook:
	/sbin/chkconfig --add lldpad || true
	## provide legacy support for apps that use the old dcbd interface.
	test -e $(includedir)/dcbd || `ln -T -s $(includedir)/lldpad $(includedir)/dcbd`
	test -e $(includedir)/dcbd/clif_cmds.h || `ln -T -s $(includedir)/lldpad/lldp_dcbx_cmds.h $(includedir)/dcbd/clif_cmds.h`

uninstall-local:
	/sbin/chkconfig --del lldpad || true
	rm -f '$(DESTDIR)/etc/init.d/lldpad'
	rm -f '$(includedir)/dcbd/clif_cmds.h'
	rm -f '$(includedir)/dcbd'

