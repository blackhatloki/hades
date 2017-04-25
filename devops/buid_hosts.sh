#!/bin/bash 

wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' > all_hosts_Cluster_prince
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep home > all_hosts_home
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep log  > all_hosts_log
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep ^c  > all_hosts_compute
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep ^gpu  > all_hosts_gpu
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep ^beegfs  > all_hosts_beegfs
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep ^ufm  > all_hosts_ufm
wwsh node list | awk -F" " ' { printf "%s\n",$1 } ' | grep ^prince  > all_hosts_prince

pdsh -w ^all_hosts_gpu "ipmitool lan print 1 | egrep \"IP Address  |Subnet Mask|MAC Address\" " > ipmi-ip.gpu
pdsh -w ^all_hosts_compute "ipmitool lan print 1 | egrep \"IP Address  |Subnet Mask|MAC Address\" " > ipmi-ip.compute
pdsh -w ^all_hosts_ufm "ipmitool lan print 1 | egrep \"IP Address  |Subnet Mask|MAC Address\" " > ipmi-ip.ufm
pdsh -w ^all_hosts_log "ipmitool lan print 1 | egrep \"IP Address  |Subnet Mask|MAC Address\" " > ipmi-ip.log
pdsh -w ^all_hosts_home "ipmitool lan print 1 | egrep \"IP Address  |Subnet Mask|MAC Address\" " > ipmi-ip.home
pdsh -w^all_hosts_Cluster_prince "dmidecode | grep -i product | head -1"  > hardware
pdsh -w^all_hosts_Cluster_prince "dmidecode | grep -i \"serial number\" | head -1"  > serial
