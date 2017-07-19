#!/bin/bash 
$username=$1
ipcs -s | awk '/$username/ {system("ipcrm -s" $2)}'   
