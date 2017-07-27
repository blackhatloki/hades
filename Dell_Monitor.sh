#!/bin/bash 
value=`dmidecode | grep "Vendor:"  | sed -e 's/^\s*//g' | sed -e 's/ Dell Inc\./Dell/g' |  awk -F":" ' { print $2 } '`
if [ $value = "Dell" ] ; then 
   /home/teague/dellHardware.chk 
fi 
