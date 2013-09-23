#!/bin/bash
#Trivial wrapper script: reset the NI4462: "ni4462_test -S"

if [ $# != 0 ] ;then
	echo "This is a trivial wrapper script: self-calibrate the NI4462: 'ni4462_test -S'. No args."
	exit 1
else
	ni4462_test -S
	exit $?
fi
