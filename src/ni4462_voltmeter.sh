#!/bin/bash

#Trivial script: acts like a voltmeter, measures all channels for 1 second, outputs statistics.
#Copyright 2012 Richard Neill <ni4462 at richardneill dot org>. This is Free Software, Licensed under the GNU GPL v3+.

#Sample for 1 second, print mean/stddev.
#Alternatively (with -r), show realtime updates, and never exit till Ctrl-C, like a regular digital voltmeter..

BINARY=ni4462_test

CHANNEL=all		#default channel.
FREQ=200000		#200k
NUM=$FREQ		#1 second. Be wary of shorter times: mains hum isn't apparent at much shorter timescales.
VOLT_RANGE=10		#Could also be 0.3, 1, 3, 10, 30, 100
COUPLING=dc		#DC coupling (could be AC)
MODE=diff		#Differential mode (could be pdiff)
TRIGGER=now		#Trigger immediately
GAIN_ISNEW=""		#Use "-g" to allow preamp 1 second to settle; avoids possible transient. BUT, takes longer to run; probably a bad tradeoff for a voltmeter. "" to disable.

RT_FREQ=1000		#Realtime sample freq. (gets decimated anyway, not too fast or we overflow buffer)
RT_RATE=4		#Update rate on screen
DECIMATE=$((RT_FREQ/RT_RATE))

VOLT_RANGE_OPTS="0.316, 1, 3.16, 10, 31.6, 42"  #Ranges for the 4462. Other values get coerced.

#Check programs.
if ! which unbuffer > /dev/null; then   #Newer systems could use coreutils:stdbuf, but the NI's kernel module forces an older distro, which hasn't got it.
	echo "Error: need to have the expect program 'unbuffer' installed." ; exit 1
fi
if ! which $BINARY > /dev/null; then
 	echo "Error: need to have the NI 4422  program '$BINARY' installed." ; exit 1
fi

#Parse args. Allow optional -r for realtime, -v for voltage, -c for channel, -h for help. 
while getopts rhsv:c: OPTION; do
        case "$OPTION" in
		r) SAMPLE_MODE=realtime;;
		h) HELP=true;;
		s) SCROLL=true;;
		v) VOLT_RANGE="$OPTARG";;
		c) CHANNEL="$OPTARG";;
        esac
done

#validate voltage range
if ! [[  "$VOLT_RANGE" =~ ^[0-9.]+$ ]] ;then
	echo "Invalid voltage range: $VOLT_RANGE"
	exit 1
fi

#change "\r" to "\n" in the scrolling mode.
if [ -n "$SCROLL" ]; then
	CHR="n";
else
	CHR="r"
fi

#ui
if [ "$CHANNEL" == all ] ;then
	CH_LIST=" Channels: 0, 1, 2, 3."
fi


#Sample on all channels, voltage scale 1V (unless overridden), dc coupled, differential mode, trigger immediate, show statistics, discard raw data.
CMD_SAMPLE="$BINARY -c $CHANNEL -v $VOLT_RANGE -n $NUM -f $FREQ -i $COUPLING -m $MODE -t $TRIGGER $GAIN_ISNEW -s /dev/null"

#Realtime mode: Sample on all channels constantly; then decimate; then print using \r instead of \n.
#Note unbuffering needed to prevent the pipe queuing data. Unbuffer is in "expect"; newer coreutils have stdbuf. 'tr "\n" "\r"' is problematic with unbuffer; use perl.
CMD_REALTIME="$BINARY -c $CHANNEL -v $VOLT_RANGE -n cont -f $RT_FREQ -i $COUPLING -m $MODE -t $TRIGGER $GAIN_ISNEW - | sed -u -n '0~${DECIMATE}p' | unbuffer -p perl -pe 's/\n/\\$CHR/' "

#Help
if [ -n "$HELP" ] ; then
	echo ""
	echo "Use the NI4462 to act like a voltmeter:"
	echo "  * Sampling mode: read for 1 second (at $FREQ Hz); print mean/std-dev for all channels."
	echo "  * Realtime mode: print rolling updates ${RT_RATE}x per second, like a multimeter."
	echo ""
	echo "USAGE:     `basename $0 .sh`  [OPTIONS]        "
	echo "OPTIONS:      -v        VOLT_RANGE           Voltage range. Options: $VOLT_RANGE_OPTS. default: $VOLT_RANGE V"
	echo "              -c        CHANNEL              Channel number. Options: 0,1,2,3,all.  default: $CHANNEL"
	echo "              -r                             Realtime mode: constantly update terminal, like a DVM."
	echo "              -s                             Scroll: make realtime mode print newlines between samples."
	echo "              -h                             Show this help."
	echo ""
	echo "To change options, edit this (simple) shell-script, which wraps '$BINARY' to run one of the commands:"
	echo "  $CMD_SAMPLE "
	echo "  $CMD_REALTIME "
	echo ""
	exit 1
fi

#Trap ctrl-C: restore terminal cursor, and put a blank line.
ctrlc(){
	tput cnorm
	echo ""
	exit
}
trap ctrlc SIGINT SIGQUIT SIGTERM

#Run the command.
if [ "$SAMPLE_MODE" != "realtime" ]; then
	eval $CMD_SAMPLE
else
	echo "Acquiring on channel $CHANNEL.$CH_LIST Ctrl-C to stop."
	tput civis	#terminal cursor invisible, for clarity.
	eval $CMD_REALTIME
fi

exit $?