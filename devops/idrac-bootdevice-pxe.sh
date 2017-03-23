#!/bin/bash 
. ./ENV.sh
sshpass -p $PASS ssh -n -o StrictHostKeyChecking=no -l $USER $1 racadm set iDRAC.serverboot.BootOnce  1
sshpass -p $PASS ssh -n -o StrictHostKeyChecking=no -l $USER $1 racadm set iDRAC.serverboot.FirstBootDevice PXE
sshpass -p $PASS ssh -n -o StrictHostKeyChecking=no -l $USER $1 racadm serveraction powercycle
