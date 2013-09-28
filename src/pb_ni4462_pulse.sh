#!/bin/bash
#This loads the PulseBlaster with a trigger+pulse program to aid in calibrating the delay-line needed, and in measuring the analog "setup and hold times" for the NI's input.

#Copyright (C) Richard Neill 2012-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software. You can
#redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
#either version 3 of the License, or (at your option) any later version. There is NO WARRANTY, neither express nor implied.
#For the details, please see: http://www.gnu.org/licenses/gpl.html

PB_TRIGGER_BIT=15		#Pulseblaster's output bit that is connected to the NI trigger input.
PB_ANALOG_BIT=7			#Pulseblaster's output bit that connects to the NI's analog data input.
NI4462_TRIGGER=PFI0		#Name of the NI trigger connection.
NI4462_ANALOG="ai3,Q3+"		#Name of the NI analog input connection.
NI4462_RTSI6="RTSI6"		#Name of the NI sample-clock re-sync for pulseblaster HW_Trigger.
TICK_NS=10			#Number of ns per pulseblaster tick (varies for pulseblaster model).
PB_MIN_TICKS=9			#Pulseblaster minimum instruction length.
PB_MAX_TICKS=0xffffffff		#Pulseblaster maximum instruction length.
PB_INIT=pb_init			#Pulseblaster initialisation program
PB_START=pb_start		#Pulseblaster start program
INITIAL_WAIT_MS=100		#Wait after trigger (ms). > 0.  This is the constant delay between PB trigger and the FE of PB_TRIGGER_BIT.
FINAL_WAIT_MS=50		#Final wait. (ms) > 0.
STRICT_TRIGGER_LATENCY=true	#Is the trigger-latency forced to be strictly constant? true/false. Recommend true (but this constrains $1 to be >= - INITIAL_WAIT_MS.)

#Usage.
if [ "$1" == -h -o "$1" == --help ]; then
	cat <<-EOT
	This script allows exact measurement of the required delay-compensation for the NI4462; also characterisation of
	of the ADC sampling: the analog 'setup-and-hold time' aka preamp-bandwidth, aka 'leakage' between adjacent samples.
	Each invocation allows for 2 measurements: the trigger delay and the analog response to a (perhaps short) pulse.
	
	The PulseBlaster is connected to the NI4462, such that:
	   * PB output (bit $PB_TRIGGER_BIT) operates the NI4462's edge-trigger, $NI4462_TRIGGER, via a Delay-line (for the '63-sample delay').
	   * PB output (bit $PB_ANALOG_BIT) connects to the NI4462's Analog input, $NI4462_ANALOG, via an SPDT relay (see: filterctl -k).
	
	This program initialises the PB with the Trigger ($PB_TRIGGER_BIT) low and the Analog bit ($PB_ANALOG_BIT) high.
	It then loads the PulseBlaster with a program that, when started (by a subsequent invocation of '$PB_START'), does:
	   * Receives the trigger: Section: *. Waits an initial delay of $INITIAL_WAIT_MS ms: Section: P.
	   * Triggers the NI4462, pulling the trigger bit (bit $PB_TRIGGER_BIT / $NI4462_TRIGGER) low.
	   * Waits for a configurable delay (Arg \$1): Section 1.
	   * Takes the analog pin high (bit $PB_ANALOG_BIT / $NI4462_ANALOG [provided 'filterctl -k' enabled the loopback]).
	   * Waits for a configurable delay (Arg \$2) for the pulse to finish: Section: 2.
	   * Restores the initial state (Trigger bit low, Analog bit high), waits $FINAL_WAIT_MS ms: Section: E.
	
	   Trigger:      -------___________________-----          P and E are fixed at $INITIAL_WAIT_MS ms, $FINAL_WAIT_MS ms.
	   Analog:       ________________----------_____          1 (arg 1) is delay between trigger and pulse.
	   Section:      *..P...|...1....|....2....|..E..         2 (arg 2) is length of the pulse.
	
	TRIGGER LATENCY:
	The trigger latency is the interval from PB start (HW_Trigger) to the fe of the Trigger output (bit $PB_TRIGGER_BIT / $NI4462_TRIGGER).
	  * positive delays (T1 >= 0):            the trigger latency is always exactly constant, P = $INITIAL_WAIT_MS ms.
	  * negative delays (-$INITIAL_WAIT_MS ms <= T1 < 0):  also constant (pulse starts earlier, rather than trigger starting later).
	  * large negative delays (T1 < -$INITIAL_WAIT_MS ms): not possible; such values are illegal: (STRICT_TRIGGER_LATENCY=$STRICT_TRIGGER_LATENCY).
	This is important because of the way the PulseBlaster's HW_Trigger is re-synchronised to the NI4462's sample clock via
	$NI4462_RTSI6; whatever the value (or sign) of T1, the $NI4462_TRIGGER will always gate exactly $INITIAL_WAIT_MS ms after a sample-clock. So we can
	correctly experiment, changing pulse delay/length (T1/T2) smoothly, without causing a discontinuity as T1 crosses zero.
	
	USAGE:
	   `basename $0`    T1     T2
	        #T1 = delay {trigger_falling_edge -> signal_rising_edge}. Integer + suffix (s/ms/us/ns).
	        #T2 = length of pulse (signal high).  Positive integer + suffix (s/ms/us/ns).
	
	EXAMPLES:
	   `basename $0`   315us   1ms        #Trigger $NI4462_TRIGGER; wait 315 us; signal $NI4462_ANALOG. 1ms pulse.
	   `basename $0`     0     1ms        #If delay is zero, both initial-edges are simultaneous.
	   `basename $0`   -20s    1ms        #If negative, the $NI4462_ANALOG pulse begins *before* the $NI4462_TRIGGER trigger edge.
	
	EOT
	#Trigger latency: basically we're just making sure that the time from: PB_HW_Trigger (aligned with sample clock) --> PFI0 falling edge is always  
	#constant, even if T1 becomes negative. (resulting in a VLIW program structure). When T1 is negative, we delay the pulse less and less, rather than
	#increasing the trigger-delay. This makes everything more consistent,
	#(side-effect: unless STRICT_TRIGGER_LATENCY is set to false, T1 cannot be large and negative)
	exit 0
fi

#Check for bc installed:
which bc >/dev/null 2>&1 || { echo "Error: bc not installed." && exit 1; }

#Two, required args: the two pulse lengths
if [ $# != 2 ]; then
	echo "Wrong arguments. Use -h for help."
	exit 1
fi

if [[ "$1" =~ ^(-?[0-9]+)(s|ms|us|ns)$ ]]; then		#First arg, eg  12ms or -12s.
	PERIOD=${BASH_REMATCH[1]}
	SUFFIX=${BASH_REMATCH[2]}

	if [ "$SUFFIX" == 's' ]; then
		LEN1_NS=$((PERIOD*1000000000))
	elif [ "$SUFFIX" == 'ms' ]; then
		LEN1_NS=$((PERIOD*1000000))
	elif [ "$SUFFIX" == 'us' ]; then
		LEN1_NS=$((PERIOD*1000))
	else
		LEN1_NS=$((PERIOD))
	fi
elif [  "$1" == "0" ];then	# 0
	LEN1_NS=0
else
	echo "First length must be an integer with units, eg '-5us' or '3ms'. Use -h for help."
	exit 1
fi

if [[ "$2" =~ ^(-?)([0-9]+)(s|ms|us|ns)$ ]]; then	#2nd arg, eg  12ms or 2s.
	NEGATIVE_2=${BASH_REMATCH[1]}
	PERIOD=${BASH_REMATCH[2]}
	SUFFIX=${BASH_REMATCH[3]}

	if [ "$SUFFIX" == 's' ]; then
		LEN2_NS=$((PERIOD*1000000000))
	elif [ "$SUFFIX" == 'ms' ]; then
		LEN2_NS=$((PERIOD*1000000))
	elif [ "$SUFFIX" == 'us' ]; then
		LEN2_NS=$((PERIOD*1000))
	else
		LEN2_NS=$((PERIOD))
	fi
else
	echo "Second length must be a positive integer with units, eg '5us' or '3ms'. Use -h for help."
	exit 1
fi
if [ "$2" == "0" -o $LEN2_NS == 0 -o -n "$NEGATIVE_2" ];then
	echo "Error: 2nd arg must be positive."
	exit 1
fi


#First, initialise the pulseblaster (bit 15 high, bit 14 low, all the rest (that we don't care about) low. Actually do this now.
printf -v OUTPUT0 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
echo "Initialising PulseBlaster with bit pattern: $OUTPUT0";
$PB_INIT $OUTPUT0
if [ $? != 0 ];then
	echo "Failed to initialise PulseBlaster with: $PB_INIT $OUTPUT0"
	exit 1
fi

#Calculate the program values.
DELAY0_TICKS=$((INITIAL_WAIT_MS * 1000000 / TICK_NS)) #convert ms to ticks.

#Zero delay: $1 == 0.
if [[ $LEN1_NS -eq 0 ]]; then	#Both together?
	printf -v OUTPUT1 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
	printf -v OUTPUT2 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
	printf -v OUTPUT3 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
	DELAY1_TICKS=$((LEN2_NS/TICK_NS))
	DELAY2_TICKS=$PB_MIN_TICKS		#padding
	DELAY3_TICKS=$PB_MIN_TICKS		#padding

#Positive (normal) delay: $1 > 0
elif [[ $LEN1_NS -gt 0 ]]; then
	printf -v OUTPUT1 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
	printf -v OUTPUT2 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
	printf -v OUTPUT3 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
	DELAY1_TICKS=$((LEN1_NS/TICK_NS))
	DELAY2_TICKS=$((LEN2_NS/TICK_NS))
	DELAY3_TICKS=$PB_MIN_TICKS		#padding

#Negative delay (pulse before trigger). NB, make the pulse start earlier, NOT make the trigger later. (See above for why).
elif [[ $LEN1_NS -lt 0 ]]; then
	LEN1_NS=$((-1*$LEN1_NS))  #flip the sign.

	#Important: reduce DELAY0_TICKS, so that the time when PB_TRIGGER_BIT falls is constant wrt PB_HW_TRIGGER.
	DELAY0_TICKS=$((DELAY0_TICKS - (LEN1_NS/TICK_NS) ))

	#Sanity check this isn't negative; permit override?
	if [[ DELAY0_TICKS -lt 0 ]]; then
		if [ "$STRICT_TRIGGER_LATENCY" != "true"  ]; then
			echo "Warning: the pulse starts too early wrt the trigger-delay. Extending the latter above $INITIAL_WAIT_MS ms; non-constant latency!"
			DELAY0_TICKS=$PB_MIN_TICKS
		else 
			echo "Error: the pulse starts too early wrt the trigger-delay of $INITIAL_WAIT_MS ms. Latency would vary. (to override, disable STRICT_TRIGGER_LATENCY)"
			exit 1
		fi
	fi

	if [[ $LEN1_NS -gt $LEN2_NS ]];then	#Short pulse which is over before the trigger starts.
		printf -v OUTPUT1 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT2 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT3 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
		DELAY1_TICKS=$((LEN2_NS/TICK_NS))
		DELAY2_TICKS=$(((LEN1_NS - LEN2_NS)/TICK_NS))
		DELAY3_TICKS=$PB_MIN_TICKS	#important

	elif [[ $LEN1_NS -lt $LEN2_NS ]];then	#Pulse continues till after the trigger.

		printf -v OUTPUT1 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT2 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT3 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
		DELAY1_TICKS=$((LEN1_NS/TICK_NS))
		DELAY2_TICKS=$(((LEN2_NS - LEN1_NS)/TICK_NS))
		DELAY3_TICKS=$PB_MIN_TICKS	#important

	else					#Pulse continues till exactly the trigger, i.e. $1 == -$2
		printf -v OUTPUT1 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT2 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
		printf -v OUTPUT3 0x%06x $(( ( 0 << PB_TRIGGER_BIT ) | ( 0 << PB_ANALOG_BIT ) ))
		DELAY1_TICKS=$((LEN1_NS/TICK_NS))
		DELAY2_TICKS=$PB_MIN_TICKS	#important
		DELAY3_TICKS=$PB_MIN_TICKS	#padding
	fi
fi


#Section E. Both back to initial state (15 high, 14 low) for 50ms
OUTPUT_FINAL=$OUTPUT0
FINAL_TICKS=$((FINAL_WAIT_MS * 1000000 / TICK_NS)) #convert ms to ticks.

#Verify times are valid.
if [[ ($DELAY0_TICKS -lt $PB_MIN_TICKS) || ($DELAY0_TICKS -gt $PB_MAX_TICKS) ]];then
	echo "Error: initial interval $DELAY0_TICKS is out of range. Min is $PB_MIN_TICKS; max is $PB_MAX_TICKS";
	exit 1
elif [[ ($DELAY1_TICKS -lt $PB_MIN_TICKS) || ($DELAY1_TICKS -gt $PB_MAX_TICKS) ]];then
	echo "Error: 1st interval $DELAY1_TICKS is out of range. Min is $PB_MIN_TICKS; max is $PB_MAX_TICKS";
	exit 1
elif [[ ($DELAY2_TICKS -lt $PB_MIN_TICKS) || ($DELAY2_TICKS -gt $PB_MAX_TICKS) ]];then
	echo "Error: 2nd interval $DELAY2_TICKS is out of range. Min is $PB_MIN_TICKS; max is $PB_MAX_TICKS";
	exit 1
elif [[ ($DELAY3_TICKS -lt $PB_MIN_TICKS) || ($DELAY3_TICKS -gt $PB_MAX_TICKS) ]];then
	echo "Error: 3rd interval $DELAY3_TICKS is out of range. Min is $PB_MIN_TICKS; max is $PB_MAX_TICKS";
	exit 1
elif [[ ($FINAL_TICKS -lt $PB_MIN_TICKS) || ($FINAL_TICKS -gt $PB_MAX_TICKS) ]];then
	echo "Error: final interval $FINAL_TICKS is out of range. Min is $PB_MIN_TICKS; max is $PB_MAX_TICKS";
	exit 1
fi

#Now build the program.
VLIW=$(cat <<-EOT
//OUTPUT	OPCODE		ARG	LENGTH		//comment

//Initial state and wait (should change nothing thanks to pb_init, or previous invocation of this program.)
$OUTPUT0	CONT		-	$DELAY0_TICKS

//First output
$OUTPUT1	CONT		-	$DELAY1_TICKS

//Second output
$OUTPUT2	CONT		-	$DELAY2_TICKS

//Third output
$OUTPUT3	CONT		-	$DELAY3_TICKS

//Finish: back to initial state, wait $FINAL_WAIT_MS ms, then stop (with outputs unchanged).
$OUTPUT_FINAL	CONT		-	$FINAL_TICKS
-		STOP		-	-
EOT
)

#Program it.
#echo "$VLIW"; exit;
echo -e "Programming PulseBlaster to produce a calibration sequence: Signal ($NI4462_ANALOG) follows Trigger ($NI4462_TRIGGER) by $1, Signal is high for $2 ..."
echo "$VLIW" | pb_asm - - | pb_prog -		#Assemble and Program.
if [ $? != 0 ]; then
	echo "Error: failed to program PulseBlaster."
	exit 1
fi

echo -e "\nNow run $PB_START (or HW_Trigger). This will generate the timing sequence."
exit 0
