#!/bin/bash 
# Shutdown lustre 

PATH=/sbin:/bin:/usr/bin:/usr/sbin:/usr/ucb
export 

umount -f -v /scratch 
lctl list_nids 
lustre_rmmod
lustre_rmmod
lustre_rmmod
lustre_rmmod
lustre_rmmod
lustre_rmmod
lctl list_nids 
lctl net down 
