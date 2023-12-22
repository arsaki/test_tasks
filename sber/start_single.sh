#!/bin/sh

USER=`whoami`
if [ $USER != "root" ]
then
	echo "Script must be executed by root user!"
	exit 1
fi

echo -n "Loading driver in single mode.....			"
insmod sbertask.ko mode=single
if [ $?  = 0 ]
then
	echo "Successful."
else
	echo "Error."
	exit 2
fi

echo -n "Acquiring driver number.....		"
MAJOR_NUM=`grep "sbertask" /proc/devices | awk '{print $1}'`
if [ -n $MAJOR_NUM ]
then
	echo "$MAJOR_NUM"
else
	echo "Device not found."
	rmmod sbertask
	exit 3
fi

echo -n "Creating char device /dev/sbertask..... "
mknod -m=666 /dev/sbertask c $MAJOR_NUM 0
if [ -c /dev/sbertask ]
then
	echo "Successful."
	exit 0
else
	echo "No char device founded."
	rmmod sbertask
	exit 4
fi







