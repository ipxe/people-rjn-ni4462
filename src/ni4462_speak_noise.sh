#!/bin/bash
#This is useful for making repeated measurements with the hands engaged in testing different shield configurations.
#It loops, making measurements of noise, and speaking them. The output is in mV.
#Copyright 2011-2012 Richard Neill <ni4462 at richardneill dot org>. This is Free Software, Licensed under the GNU GPL v3+.

CHANNEL=0		#0,1,2,3 or sum

FREQ=200000		#200k
NUM=$FREQ		#1 second. Be wary of shorter times: mains hum isn't apparent at much shorter timescales. Also, making this large makes the std-dev less
			#variable from one time to the next when the conditions are the SAME, so that differences in the figure may be assumed to be meaningful.

VOLTRANGE=0.316		#Max sensitivity
COUPLING=dc		#DC coupling (could be AC)
MODE=diff		#Differential mode (could be pdiff)
TRIGGER=now		#Trigger immediately
#NB for speed, don't add the "-g" option to ni4462_test; unnecesary in repeat measurements.

SLEEP=0.1		#Sleep time in loop. (Non-zero, or it's hard to quit the while loop!)

if [ "$1" == 0 -o "$1" == 1  -o "$1" == 2 -o "$1" == 3 -o "$1" == sum -o "$1" == all ] ;then
	CHANNEL=$1
else
	echo "This is a helpful utility for debugging hands-free. It acts like a multimeter, constantly reading samples"
	echo "on one (or all) channel(s), calculating the standard deviation in uV, and speech-synthesisising it."
	echo "This is useful when trying to localise sources of noise, especially given that human arms act like antennas!"
	echo "Sampling: $NUM times at $FREQ Hz"
	echo "USAGE:  `basename $0 .sh`  CHANNEL  #channel is one of: 0,1,2,3,all,sum "
	echo "For other options, edit this (simple) shell-script."
	exit 1
fi


#Check
if ! which ni4462_test > /dev/null; then
	echo "Error: need to have the NI DAQ program 'ni4462_test' installed." ; exit 1
elif ! which festival > /dev/null; then
	echo "Error: need to have the speech synthesis program 'festival' installed."; exit 1
fi

#Build the command:
CMD="ni4462_test -v $VOLTRANGE -c $CHANNEL -f $FREQ -n $NUM -i $COUPLING -m $MODE -t $TRIGGER -b /dev/null"

echo "Running (repeatedly) the command: '$CMD' ..."
echo "Use Ctrl-C several times(!) to stop."
echo "Result is the standard deviation, in uV, of channel $CHANNEL."

#test: CMD="echo 157.6"

#Loop forever.
while : ; do
	$CMD 2>&1 | tee /dev/stderr | sed 's/#.*//' | festival --tts
	sleep $SLEEP
done

