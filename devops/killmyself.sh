#!/bin/bash 

OLDIFS=IFS
IFS='
'
for i in `ps -ef | grep teague | egrep -v "root|ps|killmyself" | awk -F" " ' { print $2 } ' ` ; do 
IFS=OLDIFS
echo $i 
kill -9 $i 
IFS='
'
done 
exit 0 
