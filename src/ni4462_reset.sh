#!/bin/bash
#Trivial wrapper script: reset the NI4462: "ni4462_test -RQ"

if [ $# != 0 ] ;then
	echo "This is a trivial wrapper script: reset the NI4462: 'ni4462_test -RQ'. No args."
	exit 1
else
	ni4462_test -RQ
	exit $?
fi
