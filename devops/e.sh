wwsh -y  node set prince-master1 --netdev=eno1 --hwaddr=a0:36:9f:b3:4e:c6
wwsh -y  node set prince-master1 -D eno1 -I 172.16.0.17
wwsh -y  node set prince-master1 -D ib0 -I 10.0.0.17
wwsh -y  object modify -s IPMI_IPADDR=192.168.0.3 prince-master1
wwsh -y  node set prince-master1 -D ib7 -I 10.0.6.17 -M 255.255.252.0
wwsh -y  provision set prince-master1 --fileadd ifcfg-ib0_0 
wwsh -y  provision set prince-master1 --bootlocal=normal
