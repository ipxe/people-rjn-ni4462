#!/bin/bash
#Generate a trigger and a loopbacked data-pulse for the NI4462. Plot a graph.
#This allows characterisation of the exact delay-length, and the input characteristics (setup/hold time of the ADC, preamp bandwidth, etc)

#Copyright (C) Richard Neill 2012-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software. You can
#redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
#either version 3 of the License, or (at your option) any later version. There is NO WARRANTY, neither express nor implied.
#For the details, please see: http://www.gnu.org/licenses/gpl.html

# --- CONFIGURATION ---
#Delay length:
ARDUINO_DELAY_LEN="0us"

#Pulses:
PB_PULSE_DELAY="0ms"
PB_PULSE_LEN="1ms"

#NI4462 sample options:
PRETRIGGER_SAMPLES=2
NUMBER_OF_SAMPLES=50
DISCARD_INITIAL=50
FREQUENCY=200000
TRIGGER_EDGE="fe"
INTERNAL_EDGE="fe"
ENHANCED_LFAR="off"
TERMINAL_MODE="pdiff"
INPUT_COUPLING=dc
VOLTAGE_RANGE=10

#Input Channel.
CHANNEL_3=3		#this is the loopback channel on Q3 (see filterctl -k)

# --- END CONFIG ------


#Parse args.
while getopts hrwda:b:c:p:n:j:f:t:e:o:m:l:i:v: OPTION; do
        case "$OPTION" in
		h) SHOW_HELP=true;;
		r) DO_RESET=true;;
		a) PB_PULSE_DELAY="$OPTARG";;
		b) PB_PULSE_LEN="$OPTARG";;
		c) ARDUINO_DELAY_LEN="$OPTARG";;
		d) DEBUG=true;;
		p) PRETRIGGER_SAMPLES="$OPTARG";;
		n) NUMBER_OF_SAMPLES="$OPTARG";;
		j) DISCARD_INITIAL="$OPTARG";;
		f) FREQUENCY="$OPTARG";;
		t) TRIGGER_EDGE="$OPTARG";;
		e) INTERNAL_EDGE="$OPTARG";;
                m) TERMINAL_MODE="$OPTARG";;
		l) ENHANCED_LFAR="$OPTARG";;
                i) INPUT_COUPLING="$OPTARG";;
                v) VOLTAGE_RANGE="$OPTARG";;
                o) FILE="$OPTARG";;
                w) DO_WAIT=true;;
        esac
done

#Help
if [ -n "$SHOW_HELP" ] ;then
	cat <<-EOT

	`basename $0`:  Generate a trigger and a loopbacked data-pulse for the NI4462. Configure it, and plot a graph.
	This enables characterisation of the exact digital group-delay-length (compensate with arduino_delay), and of the
	input characteristics (setup/hold time of the ADC, pre-amp bandwidth, Gibbs phenomenon etc).
	The default settings (no args) are selected to give a helpful starting-point.

	It does the following:
	 * Initialise: set the clock to external; enable loopback on analog input (LineClock3->Q3+)
	 * Set the arduino delay line FIFO.
	 * Program the PulseBlaster to: trigger the NI; wait; send a data-pulse. Pulse may precede trigger.
	 * Set up the NI4462 sampling mode.
	 * Trigger the PB; this triggers the NI. Plot a graph.

	USAGE: `basename $0`  [OPTIONS]            #Omit all flags to use defaults.

	OPTIONS:
	  #Pulse timing options: used by 'pb_ni4462_pulse'. Units: ms/us.
	  -a PULSE_DELAY              default: $PB_PULSE_DELAY       #Delay between trigger and data pulse. (may be negative)
	  -b PULSE_LENGTH             default: $PB_PULSE_LEN       #Units: ms/us.

	  #Delay-line fifo length: used by 'arduino_delay' to delay the trigger-pulse.
	  -c ARDUINO_DELAY_LENGTH     default: $ARDUINO_DELAY_LEN       #Units: ms/us.

	  #NI4462's sampling parameters: used by ni4462_test (same flags).
	  -n NUMBER_OF_SAMPLES        default: $NUMBER_OF_SAMPLES       #Total samples to acquire (includes -p, excludes -j).
	  -p PRETRIGGER_SAMPLES	      default: $PRETRIGGER_SAMPLES       #Acquire samples before the trigger.
	  -j DISCARD_INITIAL_SAMPLES  default: $DISCARD_INITIAL        #Acquire/discard an extra j initial samples.
	  -f SAMPLE_FREQUENCY         default: $FREQUENCY    #Hz
	  -t TRIGGER_EDGE             default: $TRIGGER_EDGE        #Edge for the trigger pulse. (fe/re)
	  -e INTERNAL_EDGE            default: $INTERNAL_EDGE        #Internal clock edge. (fe/re)
	  -m TERMINAL_MODE            default: $TERMINAL_MODE     #Mode. Use pdiff, if inverting input floats. (pdiff/diff)
	  -l ENHANCED_LFAR            default: $ENHANCED_LFAR       #Enhanced LF Alias Rejection. (on/off)
	  -i INPUT_COUPLING           default: $INPUT_COUPLING        #Input coupling mode (dc/ac)
	  -v VOLTAGE_RANGE            default: $VOLTAGE_RANGE        #Voltage range (1,3,10)

	  #Miscellaneous options.
	  -o  FILENAME                save data/plot to NAME.dat, NAME.png. Use "auto" to generate name from options.
	  -d                          verbose debugging: stdout/stderr of commands won't be redirected to /dev/null.
	  -h                          show this help text.
	  -w                          wait in foreground for plot window to close; can Ctrl-C it. Default: return the prompt.
	  -r                          reset system to default state, and quit. (useful to tidy up; see also: ircam_init).

	Note that the "TTL" signal from the opto-isolator has a +ve supply adjustable from 4.5 - 5.0V. The inverting input's level
	varies - in differential mode, the dewar pulls it to VRst/2 (~0.35V), or it floats when the dewar is disconnected; in pdiff
	mode, it is pulled to 0V by 50-ohm when the dewar is disconnected, when the dewar is plugged in, it clamps VRst/2.
	Notwithstanding the amplitude, the TTL signalsare excellent square waves (up to 5 MHz).

	EOT
	exit 1
fi

#Process args, define filenames.
NI_ARGS="-p$PRETRIGGER_SAMPLES -n$NUMBER_OF_SAMPLES -j$DISCARD_INITIAL -f$FREQUENCY -t$TRIGGER_EDGE -e$INTERNAL_EDGE -m$TERMINAL_MODE -l$ENHANCED_LFAR -i$INPUT_COUPLING -v$VOLTAGE_RANGE";	#These are the NI parameters to investigate.
TITLE_ARGS="-a$PB_PULSE_DELAY -b$PB_PULSE_LEN -c$ARDUINO_DELAY_LEN $NI_ARGS"	#The title for the graph.
if [ -z "$FILE" ]; then
	DATA_FILE=$(mktemp)
else
	if [ "$FILE" == auto ];then
		FILE=ni4462char$(echo "$TITLE_ARGS" | tr -d ' ')
	fi
	DATA_FILE=$FILE.dat ; PNG_FILE=$FILE.png
	PLOT_ARG="-o $PNG_FILE"
fi
SIGNAL_FILE=$(mktemp)

#Error handling. Normally, this script is relatively quiet, compared to the noisy underlying programs.
if [ -n "$DEBUG" ] ;then
	STDOUT=/dev/stdout; STDERR=/dev/stderr
else
	STDOUT=/dev/null; STDERR=/dev/null
fi

#Clean up
clean_up(){
	rm -f $SIGNAL_FILE  #clean up outdated or broken file.
	[ "$1" == "all" -o -z "$FILE" ] && rm -f $DATA_FILE $PNG_FILE
}

#Error-check function.
error_check(){
	[ "$1" -eq 0 ] && return 0;
	echo -en "\aERROR. Last command failed!"; [ -z "$DEBUG" ] && echo -en " (Retry with -d to debug, showing stdout/stderr)."; echo -e "\n"
	clean_up all
	exit 1
}

#Check inotifywait present.
if ! which inotifywait >/dev/null 2>&1; then
	echo "Error: inotifywait is not installed. Please install it";
	exit 1 
fi

#Reset (used to tidy up afterwards, just in case)
if [ -n "$DO_RESET" ] ;then
	#Disable loopback
	echo "Disabling loopback for Analog input."
	filterctl -z > $STDOUT 2> $STDERR
	error_check $?

	echo "Resetting NI4462 and Arduino"
	ni4462_test -RQ		#Kill any tasks that may still be running.
	arduino_delay -r > $STDOUT 2> $STDERR
	error_check $?

	echo "Stopping PulseBlaster"
	pb_stop > $STDOUT 2> $STDERR
	error_check $?

	echo "Initialising PulseBlaster serial trigger"
	pb_serial_trigger -i > $STDOUT 2> $STDERR
	error_check $?

	echo "System back to default settings"
	exit 1

fi

#Trap ctrl-C: kill the dataplot program (which ignores Ctrl-C); reset the NI if still active.
ctrlc(){
	{ pgrep dataplot && killall dataplot   ; } > $STDOUT 2> $STDERR
	{ pgrep ni4462_test && killall ni4462_test ; } > $STDOUT 2> $STDERR
	ni4462_test -RQ > $STDOUT 2> $STDERR
	echo ""
	exit 1
}
trap ctrlc SIGINT

#Initialise
#Set the clock divider (on HW_Trigger) to External, Enable loopback mode.
echo "Setting clock source to external, enable loopback on analog input"
filterctl -c Ext -k > $STDOUT 2> $STDERR
error_check $?

#If necessary, kill any tasks that may previously be running.
if pgrep ni4462_test; then
	echo "Resetting NI4462."
	ni4462_test -RQ > $STDOUT 2> $STDERR
	error_check $?
fi

#Prepare the arduino and pulseblaster.
echo "Set arduino_delay to: $ARDUINO_DELAY_LEN"
arduino_delay -s $ARDUINO_DELAY_LEN > $STDOUT 2> $STDERR
error_check $?

echo "Pulseblaster: pulse_delay: $PB_PULSE_DELAY; pulse_length: $PB_PULSE_LEN"
pb_ni4462_pulse $PB_PULSE_DELAY $PB_PULSE_LEN  > $STDOUT 2> $STDERR
error_check $?
echo "Arming Pulseblaster"
pb_arm > $STDOUT 2> $STDERR
error_check $?

#Start the NI4462 in background. [NB, we MUST operate in reference trigger mode (-p >= 2) or the PB will never be able to start.]
#Important for the NI to be READY (taskStarted) before we trigger it! Must observe "NI4462 waiting for trigger." THEN "HW_Trigger: sent". 
ni4462_test -c $CHANNEL_3 $NI_ARGS -x $DATA_FILE -T $SIGNAL_FILE &
NI_PID=$!
inotifywait -q -e delete $SIGNAL_FILE > $STDOUT 2> $STDERR    #Wait using inotifywait, combined with -T above. Else, sleep 5. 

#Trigger it.		#NB pb_start is software, pb_serial_trigger activates the PB via the HW_Trigger input. Only the latter is guaranteed to
pb_serial_trigger -t	#be synchronised with the RTSI6, so as to ensure perfectly repeatable conditions. So use pb_serial_trigger instead of pb_start.
error_check $?

#Wait for NI to finish, check exit status, then plot.
wait $NI_PID		
error_check $?
echo "Data capture finished; now plotting..."

#Plot it. 
if [ -n "$DO_WAIT" ]; then	#Wait for dataplot window to close, (can Ctrl-C it)
	dataplot -t "$TITLE_ARGS" $PLOT_ARG $DATA_FILE > $STDOUT 2> $STDERR;
	error_check $?
else				#Background it, return prompt, leaving the graph open.
	dataplot -t "$TITLE_ARGS" $PLOT_ARG $DATA_FILE > $STDOUT 2> $STDERR  &
	inotifywait -q -e close $DATA_FILE > $STDOUT 2> $STDERR    #Wait for datafile to be read
fi

#Done. 
[ -n "$FILE" ] && echo "Saved data in $DATA_FILE and plot in $PNG_FILE"
clean_up
echo ""
exit 0;
