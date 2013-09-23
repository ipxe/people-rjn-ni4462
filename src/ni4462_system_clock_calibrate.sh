#!/bin/bash
#This calibrates the whole system (using the NI4462's master clock) against an external reference.
#This loads the PulseBlaster with a simple program to allow for frequency-calibration of the PB.
#The PB is gated on and off for (exactly) 10 seconds by external HW_Trigger/Reset.
#While oscillating, it emits pulses at 1 MHz.
#These pulses are counted by the arduino.
#NB the RTSI6/74VHC74 gating of HW_Trigger (necessary for other reasons) reduces the precision of this.

#Copyright 2011-2012 Richard Neill <ni4462 at richardneill dot org>. This is Free Software, Licensed under the GNU GPL v3+.


PB_TRIGGER_BIT=15		#Pulseblaster's output bit that is connected to the NI trigger input, AND the arduino counter input.
CLOCK_FREQ_MHZ=1
CLOCK_PERIOD_NS=1000		#1 MHz.   #Actually, the arduino can count at 2.5 MHz, but no point: we already have jitter from the 74HC74
TICK_NS=10			#Number of ns per pulseblaster tick (varies for pulseblaster model).
PB_MIN_TICKS=9			#Pulseblaster minimum instruction length.
PB_INIT=pb_init			#Pulseblaster initialisation program
PB_START=pb_start		#Pulseblaster start program
PB_STOP=pb_stop			#Pulseblaster stop program

if [ "$1" == "sw" ];then
	TRIGGER=sw
elif [ $# == 0 ]; then
	TRIGGER=hw
else
	echo ""
	echo "This calibrates the ircam system master clock (exported from NI 4462 by LVDS) against an external reference."
	echo "The gate (HW_Trigger/HW_Reset) enables the PB for 10 seconds; The PB emits 1 MHz; the Arduino counts pulses."
	echo "So it's possible to work out (accurately) the frequency of the system clock, in terms of the gate-interval."
	echo ""
	echo "This measurement is accurate to better than 1ppm. The sources of error are:"
	echo "  * Error in pulse-count:  1 pulse in 10 million."
	echo "  * Jitter in starting HW_Trigger (the 74VHC74/RTSI6 at 200kHz:  5us in 10 seconds."
	echo "  * Latency/Jitter in the PulseBlaster < 8 cycles (80 ns)."
	echo ""
	echo "PROCESS:"
	echo "  1.   filterctl -c Ext                                      #set clock divider (on HW_Trigger) to External"
	echo "  2.   arduino_delay -ri                                     #clear (and insert) the FIFO"
	echo "  3.   pb_freq_gen 1000000 0x8000                            #load the PulseBlaster program (1MHz)"
	echo "  4.   ni4462_test -f 200000 -n 100 -p 10 -t fe /dev/null    #start NI4462 RTSI6 at 200kHz"
	echo "  5.   arduino_delay -c 20s                                  #count pulses during the next 20 seconds"
	echo "  6.   sleep 0.1                                             #allow arduino to be ready"
	echo "  7a.  HW_Trigger -_ ; sleep 10(*); HW_Reset -_              #gate the PB on for 10s, while arduino is counting".
	echo "  7b.  pb_start; sleep 10; pb_stop                           #Alternative: in software; much less accurate."
	echo "  8.   Read the output from arduino_delay                    #divide by 10 to get the calibrated PB frequency."
	echo "  9.   Print the summary                                     #measured freq and error information"
	echo ""
	echo "USAGE: `basename $0`        #Use external hardware trigger (normal)"
	echo "       `basename $0` sw     #Use software trigger (not very accurate, uses 'sleep 10')"
	echo ""
	exit 1
fi

#Trap ctrl-C to force the NI to reset.
ctrlc(){
	echo "Resetting NI4462 and Arduino"
	ni4462_test -RQ
	arduino_delay -R
	exit 1
}
trap ctrlc SIGINT

#Set the clock divider (on HW_Trigger) to External
echo "Setting clock source to external"
filterctl -c Ext

#Clear and insert the FIFO
echo "Clearing and inserting the FIFO"
arduino_delay -ri

#Clearing any running tasks in the NI4462
echo "Resetting the NI4462"
ni4462_test -RQ

#Configure the PB to emit 1 MHz on bit 15
echo "Configuring 1 MHz on PulseBlaster bit 15"
pb_freq_gen 1000000 0x8000

#Start the NI4462's RTSI6 at 200 kHz (this enables the HW_Trigger input to work.)
#Background it. It will start later, when the PB actually is triggered. We don't care about the result, only that RTSI6 is oscillating.
#Note: The NI can take up to 1.6 seconds to configure the task (especially just after a reset); hence the sleep.
echo "Activating RTSI6 at 200 kHz"
ni4462_test -f 200000 -n 100 -p 10 -t fe /dev/null &
sleep 2

#Put the Arduino into counter mode, and count for the next 20 seconds. During this time, we should ensure the PB
#is active for 10 seconds (via the GATE)
echo "Arduino: counting pulses"
TMPFILE=$(mktemp)
arduino_delay -c 20s  > $TMPFILE &
APID=$!
sleep 0.1 	#Just in case.

#Now gate the PB for 10 seconds.
if [ $TRIGGER == "hw" ];then
	#Hardware trigger it.
	ACCURACY="0.5 ppm"   #Calculated from the various sources of jitter.
	echo ""
	echo "** NOW PLEASE TRIGGER THE PULSEBLASTER.   **"
	echo "** DURING THE NEXT 20 SECOND WINDOW, DO:  ** "
	echo "**  1. FALLING EDGE ON HW_TRIGGER         **"
	echo "**  2. WAIT EXACTLY 10 SECONDS            **"
	echo "**  3. FALLING EDGE ON HW_RESET           **"
	echo ""
else
	#Alternative, in software, much less accurate. (Uses the PC's clock to approximate the 10 seconds.
	ACCURACY="~ 2000 ppm" #Measurement of system clock - typically.
	echo "Software trigger of PB, sleep 10, Stop. Approximate only."
	pb_start; sleep 10; pb_stop
fi

#Ensure Arduino has finished, and the result will be in TMPFILE
echo "Waiting for arduino..."
wait $APID

#Read the output (and clean up).
RESULT=$(cat $TMPFILE)
rm $TMPFILE

#Maths
FREQ=$((RESULT/10))
FREQSCALE=$(echo "scale=6; $RESULT/10000000" | bc)
echo "Counted $RESULT pulses."
echo ""
echo "Measured frequency is $FREQ Hz (measurement accurate to $ACCURACY)"
echo "Frequency scale factor is: $FREQSCALE (expect 1.000).";

exit 0
