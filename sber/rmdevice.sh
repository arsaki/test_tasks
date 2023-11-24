#/bin/sh

USER=`whoami`
if [ $USER != root ]
then
	echo "Script must be executed by root user."
	exit 1
fi

rmmod sbertask
rm -f /dev/sbertask

