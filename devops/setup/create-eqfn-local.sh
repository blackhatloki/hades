#!/bin/bash
for i in `cat list5` ; do
# echo $i
# IP=`ypcat hosts | grep $i | egrep -v "ilo|vip|eqfn" | awk ' { print $1 } '`
# sed -e "s/STUB/$IP/g" ifcfg-bond0-template > ifcfg-bond0.$i
for j in eqfn1 eqfn2 ; do 
# echo $j
IP=`ypcat hosts | grep $i | egrep "$j" | awk ' { print $1 } '`
# echo "$IP $i $j " 
sed -e "s/STUB/$IP/g" ifcfg-eqfn-template > ifcfg-$j.$i
done

done
