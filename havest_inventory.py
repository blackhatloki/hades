#!/usr/bin/python

import os
import math 
import socket 
import subprocess
import re 

# retvalue = os.system("ps -p 2993 -o time --no-headers")

# proc = subprocess.Popen(['ls'], stdout=subprocess.PIPE)
#print(proc.stdout.readlines())

vendor        = subprocess.Popen('dmidecode | grep Vendor:'],stdout=subprocess.PIPE)
print(vendor.stdout.readlines())
cluster       = "prince"
country       = "USA"
city          = "New York City"
datacenter    = "South Data Center"
supportgroup  = "HPC"
room          = "NA"
cabinet       = "NA"
floor         = "12"
blade         = "NA"
rackserver    = "NA"

## 


#print retvalue 
# print vendor
