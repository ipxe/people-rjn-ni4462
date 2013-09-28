#!/bin/bash

#Because the PB now has the hardware modification with the 7474 (for synchronisation), it doesn't ordinarily respond
#to HW_Trigger, UNLESS the NI4462's RTSI6 is sending out clock pulses.
#This script activates RTSI6 till Ctrl-C'd.  It handles the Ctrl-C signal correctly.
#In burst mode (-b), it only sends a burst of pulses, rather than continuous.

#Copyright (C) 2013 Richard Neill. This is Free Software, released under the GNU GPL v3+

#Options: comment out to change
QUIET=">/dev/null 2>&1"		#Quieter?
QUIET_NI=">/dev/null 2>&1"	#NI process quiet, even on error?
#RESET="-R"			#Reset the NI4462 first? (Usually unneeded, adds latency).

#Sample as fast as possible, forever, discarding the data. Side effect (that we want) is that RTSI6 is clocked at samplerate.
#In burst mode, only sample for 100 times, then stop. A single pulse would do, but RTSI6 has a minimum length.
 CMD_CONT="ni4462_test $RESET -f 204800 -t now -n cont /dev/null"
CMD_BURST="ni4462_test $RESET -f 204800 -t now -n 100  /dev/null"
CMD=$CMD_CONT

if [ "$1" == "-b" ];then 
	CMD=$CMD_BURST
	MODE=BURST

elif [ $# -gt 0 ];then
	cat <<-EOT
	USAGE: $(basename $0)  [-b]
	Convey external HW_Trigger signals though the 7474 within 5us, for as long as this is running.
	In burst mode (-b), it sends only a burst of 100 pulses, then exits.

	Problem  #1:  The NI4462 PFI0 has severe jitter.
	Solution #1:  Ensure that the PFI0 (which is triggered by the PulseBlaster) is synchronised with the
	              NI's Sample Clock. This is done by delaying the PB's HW_Trigger with a 7474 D-Flipflop,
	              which is itself clocked by the NI4462 (exported on RTSI6).
	Problem  #2:  The modified PB now cannot be externally (manually) triggered by HW_Trigger.
	Solution #2:  Clock RTSI6 as fast as possible (204 kHz), so that HW_Trigger is copied to the PB quickly.

	This script does what is required (and does the necessary signal-handling magic); Ctrl-C to stop it.
	The underlying program is: '$CMD_CONT'.
	In burst mode, it uses:    '$CMD_BURST'.

	EOT
	exit
fi

#This is a bit tricky. ni4462_test itself intercepts (and often ignores) Ctrl-C.
#So we must run in a subshell in order for this bash script to be able to intercept the keyboard interrupt, and
#in order to respond to other signals (eg from kill) while the ni4462_test process is running.
#Then, because we have backgrounded twice, we must kill the entire process-group. $$ is the PID of this script, and -$$ is the group.
#The wait is also critical.
#Note: if this script is run, then Ctrl-Z, bg (rather than launched with &), and the user leaves it suspended for >~ 0.2s, the ni4462_test process will die from sample overflow.

if [ "$MODE" == BURST ] ;then
	echo "Now clocking the 7474 via RTSI6 for a burst of 100 pulses, at 204 kHz. This will propagate a pending trigger, if there is one."
else
	echo "Now clocking the 7474 via RTSI6 at 204 kHz. PulseBlaster will respond to HW_Trig within 5us. Ctrl-C or 'killall `basename $0`' to stop."
fi

eval echo "This process has PID: $$" $QUIET

sighandler(){
	kill -9 -- -$$
}
trap sighandler SIGINT SIGQUIT SIGTERM SIGABRT
( eval $CMD $QUIET_NI ; ) &
wait

exit;
