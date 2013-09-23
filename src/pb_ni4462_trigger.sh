#!/bin/bash
#This loads the Pulseblaster with a trivial program that will cause the NI 4462 to trigger, when pb_start is run.
#This can also set up a repeating clock, or a short pulse-train.
#Copyright 2011-2012 Richard Neill <ni4462 at richardneill dot org>. This is Free Software, Licensed under the GNU GPL v3+.

PB_BIT=15			#Pulseblaster's output bit that is connected to the NI trigger input.
NI4462_TRIG=PFI0		#Name of the NI trigger connection.
PULSE_NS=100			#Pulse length. 10ns minimum, according to NI docs.
TICK_NS=10			#Number of ns per pulseblaster tick (varies for pulseblaster model).
PB_MIN_TICKS=9			#Pulseblaster minimum instruction length.
PB_MAX_TICKS=0xffffffff		#Pulseblaster maximum instruction length.

#One, required arg: "fe" or "re", or YYms or YYms,Zx
if   [ $# == 1 -a "$1" == "fe" ] ; then
	EDGE=falling
	DIAGRAM="-_-"

elif [ $# == 1 -a "$1" == "re" ] ; then
	EDGE=rising
	DIAGRAM="_-_"

elif [[ $# == 1 && "$1" =~ ^([0-9]+)(s|ms|us|ns)$ ]]; then	#Eg  12ms
	PERIOD=${BASH_REMATCH[1]}
	SUFFIX=${BASH_REMATCH[2]}

	if [ "$SUFFIX" == 's' ]; then		#half period
		PULSE_NS=$((PERIOD*500000000))
	elif [ "$SUFFIX" == 'ms' ]; then
		PULSE_NS=$((PERIOD*500000))
	elif [ "$SUFFIX" == 'us' ]; then
		PULSE_NS=$((PERIOD*500))
	else
		PULSE_NS=$((PERIOD/2))
	fi

elif [[ $# == 1 && "$1" =~ ^([0-9]+)(s|ms|us|ns),([0-9]+)x$ ]]; then	#Eg 12ms,7x
	PERIOD=${BASH_REMATCH[1]}
	SUFFIX=${BASH_REMATCH[2]}
	REPEAT=${BASH_REMATCH[3]}

	if [ "$SUFFIX" == 's' ]; then		#half period
		PULSE_NS=$((PERIOD*500000000))
	elif [ "$SUFFIX" == 'ms' ]; then
		PULSE_NS=$((PERIOD*500000))
	elif [ "$SUFFIX" == 'us' ]; then
		PULSE_NS=$((PERIOD*500))
	else
		PULSE_NS=$((PERIOD/2))
	fi

	if [ $REPEAT == 0 ]; then
		echo "Repeat must be at least 1"
		exit 1
	fi

else
	echo "The PulseBlaster is connected to the NI 4462, such that the PB output (bit $PB_BIT) operates the NI 4462's trigger input ($NI4462_TRIG)."
	echo "This script loads a trivial program into the Pulseblaster, so that subsequently running pb_start will trigger the NI 4462."
	echo "It takes one argument: the edge to send: "
	echo "    fe         falling edge trigger: send pulse like this:  -__--   (pulse duration = $PULSE_NS ns)."
	echo "    re         rising  edge trigger: send pulse like this:  _--__   (pulse duration = $PULSE_NS ns)."
	echo "    12ms       a constantly running clock with a 12 ms time period. ('12' is any integer desired, suffix may be s,ms,us,ns)."
	echo "    20ms,7x    7 cycles of a clock, with 20 ms time period.         ('20' and '7' are integers, suffix may be s,ms,us,ns)."
	exit 1
fi


#Create the Vliw file
PULSE_TICKS=$((PULSE_NS/TICK_NS))
LEN1_TICKS=$PULSE_TICKS
LEN2_TICKS=$PULSE_TICKS
LEN3_TICKS=$((PULSE_TICKS*10))

if [[ $PULSE_TICKS -lt $PB_MIN_TICKS ]];then	#Check min len. (if smaller, nothing we can do).
	echo "Error: pulse is too short. Requested $PULSE_TICKS ticks, but minimum period is $PB_MIN_TICKS";
	exit 1
elif [[ $PULSE_TICKS -gt $PB_MAX_TICKS ]];then   #Check max len.(if larger, we could promote to longdelay). (A workaround is in pb_ni4462_calibrate.sh)
	echo "Error: pulse is too long. Requested $PULSE_TICKS ticks, but maximum period is $PB_MAX_TICKS";
	exit 1
fi

if [ "$EDGE" == "falling" ];then		#All other bits are zero (arbitrary choice)
	printf -v OUTPUT1 0x%06x $(( 1 << PB_BIT ))
	printf -v OUTPUT2 0x%06x $(( 0 ))
else
	printf -v OUTPUT1 0x%06x $(( 0 ))
	printf -v OUTPUT2 0x%06x $(( 1 << PB_BIT ))
fi
OUTPUT3=$OUTPUT1

if [ -n "$EDGE" ] ;then				#Single pulse

	VLIW=$(cat <<-EOT
	//OUTPUT  (hex) is a 3-byte value for the outputs
	//OPCODE  (string) is one of the 9 allowed opcodes. (Case-insensitive)
	//ARG     (hex) is the argument to the opcode.
	//LENGTH  (hex) is the 4-byte delay value in ticks (10ns)

	//OUTPUT	OPCODE		ARG	LENGTH		//comment

	$OUTPUT1	CONT		-	$LEN1_TICKS	//Before pulse
	$OUTPUT2	CONT		-	$LEN2_TICKS	//Pulse
	$OUTPUT3	CONT		-	$LEN3_TICKS	//Tail
	-		STOP		-	-		//Outputs unchanged; stop.
	EOT
	)

	DESCRIPTION="a $PULSE_NS ns pulse ( $DIAGRAM )"

elif [ -n "$REPEAT" ] ;then			#Many pulses

	VLIW=$(cat <<-EOT
	//OUTPUT  (hex) is a 3-byte value for the outputs
	//OPCODE  (string) is one of the 9 allowed opcodes. (Case-insensitive)
	//ARG     (hex) is the argument to the opcode.
	//LENGTH  (hex) is the 4-byte delay value in ticks (10ns)

	//OUTPUT	OPCODE		ARG	LENGTH		//comment

	$OUTPUT1	LOOP		$REPEAT	$LEN1_TICKS	//Low
	$OUTPUT2	ENDLOOP		0	$LEN1_TICKS	//High
	-		STOP		-	-		//Outputs unchanged; stop.
	EOT
	)

	DESCRIPTION="a $PERIOD ms clock with $REPEAT cycles ( _-_-_- )"

else						#Clock, forever.

	VLIW=$(cat <<-EOT
	//OUTPUT  (hex) is a 3-byte value for the outputs
	//OPCODE  (string) is one of the 9 allowed opcodes. (Case-insensitive)
	//ARG     (hex) is the argument to the opcode.
	//LENGTH  (hex) is the 4-byte delay value in ticks (10ns)

	//OUTPUT	OPCODE		ARG	LENGTH		//comment

	$OUTPUT1	CONT		-	$LEN1_TICKS	//Low
	$OUTPUT2	GOTO		0	$LEN1_TICKS	//High
	EOT
	)

	DESCRIPTION="a continuous $PERIOD ms clock ( _-_-_-... )"

fi


echo -e "Programming PulseBlaster to produce $DESCRIPTION on bit $PB_BIT ..."
echo "$VLIW" | pb_asm - - | pb_prog -		#Assemble and Program.

if [ $? != 0 ]; then
	echo "Error: failed to program PulseBlaster."
	exit 1
fi

if [ -n "$EDGE" -o -n "$REPEAT" ] ; then
	echo -e "\nNow run pb_start (or HW_Trigger) as often as desired. The PB will then trigger the NI 4462."
else
	echo -e "\nNow run pb_start (or HW_Trigger). The PB will then repeatedly trigger the NI 4462. Use pb_stop to stop."
fi
exit 0
