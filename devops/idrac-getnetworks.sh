. ./ENV.sh#!/bin/bash 
sshpass -p $PASS ssh -n -o StrictHostKeyChecking=no -l $USER $1  racadm get gethostnetworkinterfaces
