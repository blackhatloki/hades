#!/bin/bash 
. ./ENV.sh 
host=$1
#Power off a Host
ipmitool -H $host -v -I lanplus -U $USER -P $PASS  chassis power off
