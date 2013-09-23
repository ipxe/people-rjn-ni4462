#!/bin/bash
#This loads the PulseBlaster with a simple program to aid in the calibration of the NI4462 clock (wrt the PulseBlaster), and #to allow the DelayLine to be calibrated wrt the NI4462.
#WARNING: this doesn't quite do what is expected when the first arg is negative...think!

PB_TRIGGER_BIT=15		#Pulseblaster's output bit that is connected to the NI trigger input.
PB_ANALOG_BIT=7			#Pulseblaster's output bit that connects to the NI's analog data input.
NI4462_TRIGGER=PFI0		#Name of the NI trigger connection.
NI4462_ANALOG="ai3,Q3+"		#Name of the NI analog input connection.
TICK_NS=10			#Number of ns per pulseblaster tick (varies for pulseblaster model).
PB_MIN_TICKS=9			#Pulseblaster minimum instruction length.
PB_MAX_TICKS=0xffffffff		#Pulseblaster maximum instruction length.
PB_INIT=pb_init			#Pulseblaster initialisation program
PB_START=pb_start		#Pulseblaster start program
SQUARE_WAVE_COUNT=1000000
SQUARE_WAVE_FREQUENCY_KHZ=200
INITIAL_WAIT_MS=50		#Wait after initialisation. (ms)
FINAL_WAIT_MS=50		#Final wait. (ms)

#Check for bc installed:
which bc >/dev/null 2>&1 || { echo "Error: bc not installed." && exit 1; }

#Two, required args: the two pulse lengths, or "check" and offset.
if [ $# != 2 ]; then
	WRONGARGS=true
fi

if [ "$1" == check ]; then	#Check and an offset-fraction
	MODE=check
	if [[ "$2" =~ ^(0|1|0\.[0-9]+)$ ]]; then   #2nd arg: 0, 1, or 0.xxx
		OFFSET_FRACTION=$2
	else
		WRONGARGS=true
	fi
else
	MODE=pulse
	if [[ "$1" =~ ^(-?)([0-9]+)(s|ms|us|ns)$ ]]; then	#First arg, eg  12ms or -12s.
		NEGATIVE_1=${BASH_REMATCH[1]}
		PERIOD=${BASH_REMATCH[2]}
		SUFFIX=${BASH_REMATCH[3]}

		if [ "$SUFFIX" == 's' ]; then
			DELAY1_NS=$((PERIOD*1000000000))
		elif [ "$SUFFIX" == 'ms' ]; then
			DELAY1_NS=$((PERIOD*1000000))
		elif [ "$SUFFIX" == 'us' ]; then
			DELAY1_NS=$((PERIOD*1000))
		else
			DELAY1_NS=$((PERIOD))
		fi
	elif [  "$1" == "0" ];then	# 0
		DELAY1_NS=0
	else
		WRONGARGS=true
	fi

	if [[ "$2" =~ ^(-?)([0-9]+)(s|ms|us|ns)$ ]]; then	#2nd arg, eg  12ms or 2s.
		NEGATIVE_2=${BASH_REMATCH[1]}
		PERIOD=${BASH_REMATCH[2]}
		SUFFIX=${BASH_REMATCH[3]}

		if [ "$SUFFIX" == 's' ]; then
			DELAY2_NS=$((PERIOD*1000000000))
		elif [ "$SUFFIX" == 'ms' ]; then
			DELAY2_NS=$((PERIOD*1000000))
		elif [ "$SUFFIX" == 'us' ]; then
			DELAY2_NS=$((PERIOD*1000))
		else
			DELAY2_NS=$((PERIOD))
		fi
	else
		WRONGARGS=true
	fi
	if [ "$2" == "0" -o -n "$NEGATIVE_2" ];then
		echo "Error: 2nd arg must be positive."
	fi
fi

if [ -n "$WRONGARGS" ] ;then
	echo "This script allows for calibration of the NI4462's internal clock against the PulseBlaster's internal clock,"
	echo "and for calibration of the delay-line FIFO against the NI4462. It is used by ni4462_calibrate."
	echo "Each invocation allows for 2 measurements: the relative delay between the falling-edges, and the length of the low pulse."
	echo "Alternatively, (in 'check' mode), it allows measurement of how well (or badly) calibrated the NI currently is."
	echo ""
	echo "The PulseBlaster is connected to the NI4462, such that:"
	echo "    * PB output (bit $PB_TRIGGER_BIT) operates the NI4462's trigger input ($NI4462_TRIGGER), via a Delay-line (for the '63-sample delay')."
	echo "    * PB output (bit $PB_ANALOG_BIT) connects to one of the NI4462's Analog inputs ($NI4462_ANALOG), via an SPDT relay (see filterctl)."
	echo ""
	echo "MEASUREMENT MODE"
	echo "----------------"
	echo "This program first sets both bits ($PB_TRIGGER_BIT and $PB_ANALOG_BIT) high (using $PB_INIT), and waits for $INITIAL_WAIT_MS ms: Section P."
	echo "It then loads the PulseBlaster with a program that, when started (by a subsequent invocation of '$PB_START'), does:"
	echo "    * Triggers the NI4462 (pulling the trigger bit (bit $PB_TRIGGER_BIT, connected to $NI4462_TRIGGER) low."
	echo "    * Waits for a configurable delay (1st arg of this program): Section 1."
	echo "    * Takes the analog pin low (bit $PB_ANALOG_BIT, connected to $NI4462_ANALOG [provided 'filterctl -k' enabled the loopback])."
	echo "    * Holds both low for a configurable delay (2nd arg of this program: Section: 2); then takes both high again."
	echo "    * Waits $FINAL_WAIT_MS ms (both high), ready for next time (and to avoid spurious re-triggering): Section: E."
	echo ""
	echo "    Trigger:      -------___________________-----          P and E are fixed at $INITIAL_WAIT_MS ms, $FINAL_WAIT_MS ms."
	echo "    Analog:       ----------------__________-----          1 (arg 1) is between the two falling edges."
	echo "    Section:      ...P...|...1....|....2....|..E..         2 (arg 2) is when both are low, independent of 1."
	echo ""
	echo "USAGE:   `basename $0`   T1   T2       #T1 = delay{trigger_fe, signal_fe}; T2={time_for_both_low}"
	echo ""
	echo "         Times are integers with a suffix of s,ms,us,ns. (the first may be negative to reverse the order). e.g.:"
	echo "         `basename $0` 315us  1ms      #Trigger $NI4462_TRIGGER; wait 315 us; signal $NI4462_ANALOG. Hold low for 1ms."
	echo "         `basename $0` 0      1ms      #If the 1st delay is zero, both falling-edges are simultaneous."
	echo "         `basename $0` -20s   1ms      #If the 1st delay is negative, the $NI4462_TRIGGER edge *follows* the $NI4462_ANALOG edge."
	echo ""
	echo "CHECK MODE"
	echo "----------"
	echo "In check mode, the PulseBlaster triggers the NI 4462, and then sends a signal of exactly $SQUARE_WAVE_COUNT pulses at $SQUARE_WAVE_FREQUENCY_KHZ kHz."
	echo "The NI 4462, sampling at $SQUARE_WAVE_FREQUENCY_KHZ kHz should then measure alternate samples high and low; if two consecutive samples are"
	echo "the same, it can detect that it has lost sync. The earlier into the $SQUARE_WAVE_COUNT that this occurs, the worse is the problem."
	echo "The trigger (falling edge on $PB_TRIGGER_BIT) is offset by a fraction x of the first half-cycle; 0.5 gives the best chance of remaining"
	echo "synchronised, while 0 and 1.0 make it hardest (compare setup/hold time):"
	echo ""
	echo "    Analog:       --------________--------_____ ...   "
	echo "    Trigger:      ----_________________________ ...   "
	echo "                  p  x    q                                 x is the fractional trigger-position, between p...q."
	echo ""
	echo "USAGE:    `basename $0`  check  TRIGGER_OFFSET       #Trigger offset is a float from [0,1], usually 0.5."
	echo ""
	exit 1
fi

#First, initialise the pulseblaster with both bits high. Actually do this now.
printf -v BOTH_HIGH 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))
echo "Initialising PulseBlaster with bit pattern: $BOTH_HIGH";
$PB_INIT $BOTH_HIGH
if [ $? != 0 ];then
	echo "Failed to initialise PulseBlaster with: $PB_INIT $BOTH_HIGH"
	exit 1
fi

#Now wait briefly.
SLEEPTIME=$(echo "scale=4; $INITIAL_WAIT_MS/1000" | bc)
sleep $SLEEPTIME

if [ $MODE == "pulse" ];then	#Pulse mode.

	#Calculate the main program values.
	#Section 1.
	if [ $DELAY1_NS == 0 ]; then	#Both together?
		printf -v FIRST_OUTPUT 0x%06x 0
		DELAY1_TICKS=$PB_MIN_TICKS  #Can't actually wait zero. Use shortest, then correct for it below.
		CORRECTION_NEEDED=$PB_MIN_TICKS
		FIRST_LOW="Both"

	elif [ -z "$NEGATIVE_1" ]; then #Not negative, so Trigger, then Signal
		printf -v FIRST_OUTPUT 0x%06x $(( ( 1 << PB_ANALOG_BIT ) ))
		DELAY1_TICKS=$((DELAY1_NS/TICK_NS)) 	#Configured delay, converted to ns.
		FIRST_LOW=$NI4462_TRIGGER

	else				#Signal falls first, trigger later.
		printf -v FIRST_OUTPUT 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) ))
		DELAY1_TICKS=$((DELAY1_NS/TICK_NS))
		FIRST_LOW=$NI4462_ANALOG
	fi

	if [[ $DELAY1_TICKS -lt $PB_MIN_TICKS ]];then	#Check min len is valid
		echo "Error: interval is too short. Requested $DELAY1_TICKS ticks, but minimum period is $PB_MIN_TICKS";
		exit 1
	elif [[ $DELAY1_TICKS -gt $((200 * $PB_MAX_TICKS)) ]];then  #Check max len is valid. (We could repeat up to 200 delays)
		echo "Error: interval is too long. Requested $DELAY1_TICKS ticks, but maximum period (even with extra instructions) is $((200*$PB_MAX_TICKS))";
		exit 1
	elif [[ $DELAY1_TICKS -gt $PB_MAX_TICKS ]];then #Need to pad the delay in repeated groups. Only allow making it upto 200x longer; otherwise could resort to longdelay.
		while [[ $DELAY1_TICKS -gt $PB_MAX_TICKS ]]; do	#Allows up to 8400 seconds. Should be plenty!
			EXTRA_INSTRUCTIONS1="$FIRST_OUTPUT	CONT		-	$PB_MAX_TICKS	//overlong "$'\n'"$EXTRA_INSTRUCTIONS"
			DELAY1_TICKS=$((DELAY1_TICKS - PB_MAX_TICKS))
		done
	fi

	#Section 2. Hold both low for DELAY2_NS
	printf -v SECOND_OUTPUT 0x%06x 0x00  	#Both low
	DELAY2_TICKS=$((DELAY2_NS/TICK_NS)) 	#Configured delay, converted to ns.
	if [ -n "$CORRECTION_NEEDED" ]; then
		DELAY2_TICKS=$((DELAY2_TICKS - CORRECTION_NEEDED)); #If we added a shortest above, remove it here. Important, for accuracy.
	fi

	if [[ $DELAY2_TICKS -lt $PB_MIN_TICKS ]];then	#Check min len is valid
		echo "Error: interval is too short. Requested $DELAY2_TICKS ticks, but minimum period is $PB_MIN_TICKS";
		exit 1
	elif [[ $DELAY2_TICKS -gt $((200 * $PB_MAX_TICKS)) ]];then  #Check max len is valid. (We could repeat up to 200 delays)
		echo "Error: interval is too long. Requested $DELAY2_TICKS ticks, but maximum period (even with extra instructions) is $((200*$PB_MAX_TICKS))";
		exit 1
	elif [[ $DELAY2_TICKS -gt $PB_MAX_TICKS ]];then #Need to pad the delay in repeated groups. Only allow making it upto 200x longer; otherwise could resort to longdelay.
		while [[ $DELAY2_TICKS -gt $PB_MAX_TICKS ]]; do	#Allows up to 8400 seconds. Should be plenty!
			EXTRA_INSTRUCTIONS2="$FIRST_OUTPUT	CONT		-	$PB_MAX_TICKS	//overlong "$'\n'"$EXTRA_INSTRUCTIONS2"
			DELAY2_TICKS=$((DELAY2_TICKS - PB_MAX_TICKS))
		done
	fi

	#Section e. Both high for 50ms
	FINAL_VALUE=$BOTH_HIGH
	FINAL_TICKS=$((FINAL_WAIT_MS * 1000000 / TICK_NS)) #convert ms to ticks.


	VLIW=$(cat <<-EOT
	//OUTPUT  (hex) is a 3-byte value for the outputs
	//OPCODE  (string) is one of the 9 allowed opcodes. (Case-insensitive)
	//ARG     (hex) is the argument to the opcode.
	//LENGTH  (hex) is the 4-byte delay value in ticks (10ns)

	//OUTPUT	OPCODE		ARG	LENGTH		//comment

	//Both are already high, thanks to pb_init, or previous invocation of this program.

	//Take $FIRST_LOW low and wait ${1#-}.
	$FIRST_OUTPUT	CONT		-	$DELAY1_TICKS
	$EXTRA_INSTRUCTIONS1

	//Take/hold both low, and wait $2.
	$SECOND_OUTPUT	CONT		-	$DELAY2_TICKS
	$EXTRA_INSTRUCTIONS2

	//Both high, wait $FINAL_WAIT_MS ms, then stop (with outputs unchanged).
	$FINAL_VALUE	CONT		-	$FINAL_TICKS
	-		STOP		-	-
	EOT
	)

	SUMMARY_TXT="Signal ($NI4462_ANALOG) follows Trigger ($NI4462_TRIGGER) by $1, Signal is low for $2"
	SUMMARY2_TXT="This will generate the 2-edge-and-pulse timing sequence for calibration"

else	#Check mode

	#Calculate the main program values.
	HALF_PERIOD_NS=$((1000000/SQUARE_WAVE_FREQUENCY_KHZ))  #kHz to ns
	HALF_PERIOD_TICKS=$((HALF_PERIOD_NS/TICK_NS))
	LOOP_COUNT=$((SQUARE_WAVE_COUNT-1))	#exclude the first cycle.

	printf -v PRE_TRIGGER 0x%06x $(( ( 1 << PB_TRIGGER_BIT ) | ( 1 << PB_ANALOG_BIT ) ))   #Both high
	printf -v SIGNAL_HIGH 0x%06x $(( ( 1 << PB_ANALOG_BIT ) ))			       #Just signal high
	printf -v SIGNAL_LOW  0x%06x 0							       #Both low.
	PRE_OFFSET=$PRE_TRIGGER
	POST_OFFSET=$SIGNAL_HIGH

	#Trigger goes low  OFFSET_FRACTION into the first half-pulse.
	X_TIMES_HALF_PERIOD_NS=$(echo "scale=4; x=$OFFSET_FRACTION*$HALF_PERIOD_NS; scale=0; x/1" | bc);
	Y_TIMES_HALF_PERIOD_NS=$(echo "scale=4; x=(1-$OFFSET_FRACTION)*$HALF_PERIOD_NS; scale=0; x/1" | bc);

	#If that makes the smaller part too tiny, set it to 0 length. But we can't have a zero-length pulse,
	#nor can we remove the instruction without breaking the ENDLOOP's arg. So turn it into a no-op.
	#Unless the offset was exactly 0 or 1, warn.
	if [ $X_TIMES_HALF_PERIOD_NS -lt $PB_MIN_TICKS ]; then
		PRE_OFFSET=$SIGNAL_HIGH
		X_TIMES_HALF_PERIOD_NS=$PB_MIN_TICKS
		Y_TIMES_HALF_PERIOD_NS=$((HALF_PERIOD_TICKS-PB_MIN_TICKS))
		if [ $OFFSET_FRACTION != 0 ]; then
			echo "Warning: Offset fraction specified as $OFFSET_FRACTION; too small for PB minimum delay; converted to 0.";
		fi
	elif [ $Y_TIMES_HALF_PERIOD_NS -lt $PB_MIN_TICKS ]; then
		POST_OFFSET=$PRE_TRIGGER
		X_TIMES_HALF_PERIOD_NS=$((HALF_PERIOD_TICKS-PB_MIN_TICKS))
		Y_TIMES_HALF_PERIOD_NS=$PB_MIN_TICKS
		if [ $OFFSET_FRACTION != 1 ]; then
			echo "Warning: Offset fraction specified as $OFFSET_FRACTION; remnant is too small for PB minimum delay; converted to 1.";
		fi
	fi


	VLIW=$(cat <<-EOT
	//OUTPUT  (hex) is a 3-byte value for the outputs
	//OPCODE  (string) is one of the 9 allowed opcodes. (Case-insensitive)
	//ARG     (hex) is the argument to the opcode.
	//LENGTH  (hex) is the 4-byte delay value in ticks (10ns)

	//OUTPUT	OPCODE		ARG	LENGTH		//comment

	//Before start, take both high for a short while.
	$PRE_TRIGGER	CONT		-	$HALF_PERIOD_NS

	//Bring trigger low a fraction x of t/2 into the first pulse.
	$PRE_OFFSET	CONT		-	$X_TIMES_HALF_PERIOD_NS
	$POST_OFFSET	CONT		-	$Y_TIMES_HALF_PERIOD_NS

	//Second half of the first pulse
	$SIGNAL_LOW	CONT		-	$HALF_PERIOD_NS

	//Now loop ($SQUARE_WAVE_COUNT-1) times.
	$SIGNAL_HIGH	LOOP		$LOOP_COUNT	$HALF_PERIOD_NS
	$SIGNAL_LOW	ENDLOOP		4	$HALF_PERIOD_NS

	//Stop: do not change the outputs.
	-		STOP		-	-
	EOT
	)

	SUMMARY_TXT="trigger (with offset fraction: $OFFSET_FRACTION), and $SQUARE_WAVE_COUNT pulses at $SQUARE_WAVE_FREQUENCY_KHZ kHz"
	SUMMARY2_TXT="This will generate the trigger (offset: $OFFSET_FRACTION into first half of pulse) and $SQUARE_WAVE_COUNT pulses at $SQUARE_WAVE_FREQUENCY_KHZ kHz"
fi


echo -e "Programming PulseBlaster to produce a calibration sequence: $SUMMARY_TXT ..."
echo "$VLIW" | pb_asm - - | pb_prog -		#Assemble and Program.
if [ $? != 0 ]; then
	echo "Error: failed to program PulseBlaster."
	exit 1
fi

echo -e "\nNow run $PB_START (or HW_Trigger). $SUMMARY2_TXT."
exit 0
