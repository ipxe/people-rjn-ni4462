/* Calibration program for the NI 4462 clock against that of the PulseBlaster. After triggering, we receive a pulse on one input;
   the length of this pulse and the time after trigger are measured. This allows for calibration of the NI4462's clock.
   For information on how to use the device, see ni4462_test.c

   Copyright Richard Neill, 2011-2012. This would ideally be fully GPL'd free software, but it can't be (because it has to link against libnidaqmx).
   Therefore, I release it as GPL v3+, but with an exception for linking against libnidaqmx.
*/

/* Headers */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>

#include <NIDAQmx.h>					/* NI's library. Also '-lnidaqmx' */
#define LIBDAQMX_TMPDIR		"/tmp/natinst/"		/* Temp dir for NI's lock files. We clean this up below. Caution. */
#ifndef OnboardClock					/* Bugfix: defined in docs, not in header */
#define OnboardClock NULL
#endif

/* Device properties */
#define DEV_NAME			"NI 4462"		/* Product name */
#define DEV_DEV				"Dev1"			/* Which PCI device to use. */
#define DEV_TRIGGER_INPUT		"PFI0"			/* Name of the Digital trigger input */
#define DEV_FREQ_MIN			32.0			/* Min freq (Hz) */
#define DEV_FREQ_MAX			204800.0		/* Max freq (Hz) */
#define DEV_FREQ_QUANTISATION		"181.9 uS/s"		/* Frequency quantisation, to nearest micro sample-per-second */
#define DEV_SAMPLES_MIN			2			/* Min number of samples at a time. (experimentally) */
#define DEV_SAMPLES_MAX			16777215		/* 2^24 - 1, measured experimentally */
#define DEV_ADC_FILTER_DELAY_SAMPLES	63			/* ADC digital filter group delay. Constant number of samples, if Low-Frequency Alias Rejection is off. */

/* Configuration choices. */					/* (Experiment with ni4462_test.c, if desired, then set here. */
#define INPUT_CHANNEL			"ai3"			/* Channel 3. Options: ai0, ai1, ai2, ai3, ai0:3 */
#define NUM_CHANNELS			1			/* Using 1 channel */

#define VOLTAGE_MAX			10.0			/* Options: 0.316, 1, 3.16, 10, 31.6, 100.  x means [-x,+x]. */

#define INPUT_COUPLING			DAQmx_Val_DC		/* Options: DAQmx_Val_DC, DAQmx_Val_AC */
#define INPUT_COUPLING_STR		"dc"			/* NB: if AC, remember the settling time AFTER taskCommit: see manual, section: Analog Input Channel Configurations -> Input Coupling  */

#define TERMINAL_MODE			DAQmx_Val_Diff		/* Options: DAQmx_Val_Diff, DAQmx_Val_PseudoDiff */
#define TERMINAL_MODE_STR		"diff"

#define TRIGGER_EDGE			DAQmx_Val_Falling	/* Options: DAQmx_Val_Rising, DAQmx_Val_Falling */
#define TRIGGER_EDGE_STR		"falling"

#define ENABLE_ADC_LF_EAR		0			/* Don't Enable low frequency enhanced alias rejection: it can affect the filter-delay */
#define INT_CLOCK_EDGE  		DAQmx_Val_Rising	/* Sample on the RE of the Internal clock. (Probably doesn't matter). */
#define INT_CLOCK_EDGE_STR  		"rising"

/* Other */
#define LOGIC_HIGH_VOLTAGE		1.5			/* TTL-ish level threshold. Note that ai3- may be held anywhere from Gnd to +1V DC.  */
#define PULSEBLASTER_TRIGGER_BIT	15			/* PulseBlaster bit connecting to the PFI0 input */
#define PULSEBLASTER_SIGNAL_BIT		14			/* PulseBlaster bit connecting to the ai3+ input */
#define CHECK_FREQUENCY_HZ		200000			/* PulseBlaster frequency in check mode, */
#define CHECK_COUNT			100000			/* PulseBlaster count in check mode. */
#define LOGIC_UNKNOWN 			-1			/* Logic level that hasn't yet been assigned. */

/* Buffer sizes */
#define BUFFER_SIZE			(25000*NUM_CHANNELS)	/* max_number_of_data_points_read_at_a_time * number_of_channels. This is *plenty*: see ni4462_test.c */


/* Macros */
#define handleErr(functionCall) if(( he_retval=(functionCall) )){ handleErr2(he_retval); }	/* For each DAQmx call, if retval != 0, handle the error appropriately */

#define eprintf(...)	fprintf(stderr, __VA_ARGS__)				/* Error printf: send to stderr  */

#define deprintf(...)	if (debug) { fprintf(stderr, __VA_ARGS__); }		/* Debug error printf: print to stderr iff debug is set */

#define VDEBUG_MAX	100							/* Verbosity-limiting for certain debug messages: limited to 100 in total */
#define vdeprintf(...)	if (vdebugc < VDEBUG_MAX ){ deprintf ( __VA_ARGS__ ); } else if (vdebugc == VDEBUG_MAX) { deprintf ("[Verbosity limiter: maximum %d of these messages.]\n", vdebugc) ; } ; vdebugc++;

#define feprintf(...)	fprintf(stderr, __VA_ARGS__); exit (EXIT_FAILURE)	/* Fatal error printf: send to stderr and exit */

#define outprintf(...)	fprintf(outfile, __VA_ARGS__)				/* Write to output file. */


/* Globals */
int debug = 0;
int vdebugc = 0;  		/* verbosity limiting debug counter */
int32  he_retval = 0;   	/* Used by #define handleErr() above */
TaskHandle taskHandle = 0;

/* Prototypes */
void stop_clear_clean_task();

/* Show help */
void print_help(char *argv0){
	eprintf("INTRO\n"
		"-----\n"
		"This program allows for calibration of the NI 4462 clock against an external reference, usually the PulseBlaster.\n"
		"This is important because the NI 4462's triggering capability is so restricted; it's therefore necessary to stay\n"
		"synchronised with an external clock source to within < 1 sample per second, i.e. < 5 ppm across both clocks combined.\n"
		"Given the quoted accuracy of +- 20ppm (+ 5ppm yearly drift) for the NI 4462's timebase, this is only achievable by\n"
		"careful measurement and adjustment. It's rather like keeping a team of synchronised swimmers perfectly together for\n"
		"a 10-minute performance, where there is a starting-pistol, but no music!\n"
		"\n"
		"Wiring: PulseBlaster (bit %d) to Trigger (%s); PulseBlaster (bit %d) to Input (%s+); ensure %s- is grounded or\n"
		"low voltage DC. Input (%s) normally connects to the Hawaii sensor; 'filterctl -k' changes this over, via a reed\n"
		"relay. Signals are considered logic high if (%s+ - %s-) exceeds %.2f Volts.\n"
		"\n"
		"Calibration's aim is to *zero-out the trigger delay* (adjusting the fifo in the trigger-path with 'arduino_delay')\n"
		"and to *match the Pulseblaster and NI 4462 clocks* sufficiently for an entire data-frame [COAST: a whole sweep].\n"
		"\n"
		"MEASUREMENT MODE\n"
		"----------------\n"
		"This mode measures the trigger-delay, and the clock-differences.\n"
		"The trigger is produced by pb_ni4462_calibrate, and consists of pulses on %s (trigger) and %s (analog input):\n"
		"\n"
		"       Trigger:      -------___________________-----          P and E are fixed at 50 ms, 50 ms.\n"
		"       Analog:       ----------------__________-----          1 is between the two falling edges.\n"
		"       Section:      ...P...|...1....|....2....|..E..         2 is when both are low, independent of 1.\n"
		"\n"
		"This allows for two measurements to be made:\n"
		"       * The exact trigger delay (including external wiring). Nominally -%d samples. (yes, negative).\n"
		"       * The length of the pulse, which calibrates the clock speed.\n"
		"The complication: Measurements are subject to 5us jitter (at both ends): this means measurements must last at\n"
		"least 10 seconds; applying the engineering 10x rule, we get 100 seconds.\n"
		"\n"
		"CHECK MODE\n"
		"----------\n"
		"This mode confirms (or disproves) whether a calibration value is actually good enough. The PulseBlaster sends a\n"
		"trigger, then emits %d pulses at %d kHz. If the NI 4462 samples at the calibrated rate, it *should* be able to see\n"
		"a perfect sequence of highs and lows on alternate readings, for a length of n samples... is n the full %d ?\n"
		"\n"
		"USAGE\n"
		"-----\n"
		"%s  [OPTIONS] -m MODE -f FREQ -n NUM [outfile.dat]\n"
		"\n"
		"OPTIONS:  -h           show this help\n"
		"          -d           enable verbose debugging messages.\n"
		"          -x           allow overwriting of existing output file.\n"
		"\n"
		"          -m   MODE    mode is either 'measure' or 'check': as with pb_ni4462_calibrate.\n"
		"\n"
		"          -f   FREQ    sample at frequency (float, Hz). Allowed range is: [%.1f, %.1f]. FREQ is then slightly\n"
		"                       coerced by the device. Adjustments may be very fine: quantised by %s (~0.001 ppm).\n"
		"\n"
		"          -n   NUM     number of samples to acquire. Allow more than needed.\n"
		"\n"
		"          outfile	optional output file for raw data (same format as ni4462_test). Use 'dataplot' to view.\n"
		"\n"
		"OUTPUT:   -m measure:  nominal_freq, coerced_freq, n1_samples, n2_samples, t1_ns, t2_ns.\n"
		"          -m check:    nominal_freq, coerced_freq, n_fail, fail_type.\n"
		"\n"
		"EXIT:     %d           on successful measurement of 2 perfect edges, or check succeeded with perfect sync.\n"
		"          %d           on error, imperfect synchronisation in check-mode, or failure.\n"
		"\n"
		"NOTES:    * Most settings are hard-coded: Device is: %s; Channel is %s; Voltage range is: [%.1f, %.1f];\n"
		"              Coupling is %s in %s mode; Triggering is %s edge.\n"
		"          * The %d-sample ADC-delay is not compensated here. (LF_EAR is disabled).\n"
		"          * This is about calibrating the *clock* frequency against the PulseBlaster. For self-calibration of the\n"
		"              voltage-gain, use 'ni4462_test -S'. For the PulseBlaster itself, see 'pb_frequency_calibrate'.\n"
		"          * See also: 'ircam_calibrate_clocks', 'pb_ni4462_calibrate', 'filterctl -k', 'arduino_delay'.\n"
		"\n"
		,PULSEBLASTER_TRIGGER_BIT, DEV_TRIGGER_INPUT, PULSEBLASTER_SIGNAL_BIT, INPUT_CHANNEL, INPUT_CHANNEL, INPUT_CHANNEL,
		INPUT_CHANNEL, INPUT_CHANNEL, LOGIC_HIGH_VOLTAGE, DEV_TRIGGER_INPUT, INPUT_CHANNEL, DEV_ADC_FILTER_DELAY_SAMPLES,
		CHECK_COUNT, (CHECK_FREQUENCY_HZ/1000), CHECK_COUNT, argv0, DEV_FREQ_MIN, DEV_FREQ_MAX, DEV_FREQ_QUANTISATION,
		EXIT_SUCCESS, EXIT_FAILURE, DEV_DEV, INPUT_CHANNEL, -VOLTAGE_MAX, VOLTAGE_MAX, INPUT_COUPLING_STR, TERMINAL_MODE_STR,
		TRIGGER_EDGE_STR, DEV_ADC_FILTER_DELAY_SAMPLES );
}


/* Error handling: Quit on fatal errors, Print warnings and continue. */
void handleErr2 (int error){
	char  error_buf[2048]="\0", error_buf2[2048]="\0";
	if (error == 0){	/* 0 is no error */
		return;
	}
	DAQmxGetErrorString(error, error_buf, sizeof(error_buf));  /* Get long error msg */
	DAQmxGetExtendedErrorInfo (error_buf2, sizeof(error_buf)); /* Get longer error msg */

	if( DAQmxFailed(error) ){ /* i.e. (error < 0), rather than a warning */
		if( taskHandle!=0 ) {		/* Stop task, if there is one. */
			stop_clear_clean_task();
		}
		feprintf ("DAQmx Fatal Error (%d): %s\n\n%s\n\n", error, error_buf, error_buf2);
	}else if (debug){	 /* Warning: but in debug mode, so be fatal */
		if( taskHandle!=0 ) {		/* Stop task, if there is one. */
			stop_clear_clean_task();
		}
		feprintf ("DAQmx Warning (%d), with debug (-d), will exit. Error: %s\n\n%s\n\n", error, error_buf, error_buf2);
	}else{			/* Just a warning: print it and continue. */
		eprintf ("DAQmx Warning: %s\n\n%s\n\n", error_buf, error_buf2);
	}
	return;
}


/* Create, Configure and Commit the DAQmx task. See ni4462_test.c for a verbosely commented and debugging example. */
/* This function is void: if anything fails, it will result in a fatal error. Returns the coerced sample-rate by reference. */
void create_configure_commit_task(char *input_channel, uInt64 num_samples, float64 sample_rate_req, float64 *sample_rate_actual){
	/* Create Task (name is arbitrary, but avoid "" which sometimes leaks memory) */
	handleErr( DAQmxCreateTask("Calibrate",&taskHandle) );

	/* Set input channels: choose the channel(s), the terminal-mode, and the voltage-scale. NB the voltage scale is coerced by device capabilities. */
	handleErr( DAQmxCreateAIVoltageChan(taskHandle, input_channel, "AnalogQ3", TERMINAL_MODE, -VOLTAGE_MAX, VOLTAGE_MAX, DAQmx_Val_Volts, NULL) );  /* DAQmx_Val_Volts is the scale-type */

	/* Configure Channel input-coupling (AC/DC).*/
	handleErr( DAQmxSetAICoupling (taskHandle, input_channel, INPUT_COUPLING) );

	/* Configure Triggering. Set trigger input to digital triggering via the external SMA connector, and select the edge. */
	/* Comment out to trigger immediately on startTask. */
//	handleErr ( DAQmxCfgDigEdgeStartTrig (taskHandle, DEV_TRIGGER_INPUT, TRIGGER_EDGE) );
printf ("FIXME\n");
	handleErr ( DAQmxCfgDigEdgeRefTrig (taskHandle, DEV_TRIGGER_INPUT, TRIGGER_EDGE, 10) );


	/* Configure Timing and Sample Count. [Use Rising Edge of Onboard clock (arbitrary choice).] */
	/* Acquire finite number, num_samples_per_frame, of samples (on each channel) at a rate of (coereced)sample_rate_req. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, OnboardClock, sample_rate_req, INT_CLOCK_EDGE, DAQmx_Val_FiniteSamps, num_samples) );  /* Finite number of samples */
	handleErr( DAQmxGetSampClkRate (taskHandle, sample_rate_actual)  ); 	/* Check coercion */
	deprintf ("Acquiring (finite) %lld samples per task. Sample clock requested: %f Hz; actually coerced to: %f Hz. Using %s edge of the internal sample-clock.\n", num_samples, sample_rate_req, *sample_rate_actual, INT_CLOCK_EDGE_STR);

	/* Ensure that DAQmxReadAnalogF64() (below) will not block for completion, but will read all the available samples. */
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, TRUE) );

	/* Set EnhancedAliasRejectionEnable as #defined above. Off, so as to ensure ADC delay is constant. */
	handleErr( DAQmxSetAIEnhancedAliasRejectionEnable(taskHandle, input_channel, ENABLE_ADC_LF_EAR) );

	/* Commit the task (make sure all hardware is configured and ready).See notes. */
	/* If Error: DAQmxErrorPALResourceReserved  (-50103)  arises, it means two processes are contending for the device. */
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) );
}


/* Start the Task */
void start_task(){
	deprintf ("Starting task...\n");
	handleErr( DAQmxStartTask(taskHandle) );
}


/* Stop and Clear the task. Also, clean up lockfiles: libdaqmx is lazy and fails to do so. */
void stop_clear_clean_task(){
	int ret;
	handleErr( DAQmxStopTask(taskHandle) );
	handleErr( DAQmxClearTask(taskHandle) );	/* Discards task configuration */
	ret = system ( "rm -f "LIBDAQMX_TMPDIR"ni_dsc_osdep_*" );   /* Path part-hardcoded, for safety. */
}


/* Read at least 1 and up to n samples. Block till at least 1 is available; will then return as many as are available in
 * the device's buffer, subject to limit size_per_chan. Returns number of samples read, guaranteed > 1. Else, fatal error.
 * Arguments: data  (start of buffer),  size_per_chan (size of buffer in bytes per channel),  total_sofar (for debug msgs only) */
int32 blocking_read_1_to_n_samples(float64 *data, size_t size_per_chan, uInt64 total_sofar){
	int32 samples_read;

	/* Non-blocking read of all the samples that are available. May return 0 or more samples (automatically safely limited by size of buffer). */
	vdeprintf("DAQmxReadAnalogF64: Non-blocking read of as many samples as available, immediate timeout...");
	handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, size_per_chan, &samples_read, NULL) );
	vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)samples_read, total_sofar + samples_read);

	/* IFF we just got zero samples, now do a blocking read, and wait. Otherwise, optimise: guess that it won't block, so wait for the next iteration. */
	if (samples_read == 0){
		/* Blocking read of 1 sample. */
		vdeprintf("DAQmxReadAnalogF64: Blocking read of 1 sample, infinite timeout...");
		/* This is where we block, if waiting for an external trigger input */
		if ( (total_sofar == 0) ){
			deprintf ("\nWaiting for external trigger (blocking read)...\n");
		}
		handleErr( DAQmxReadAnalogF64(taskHandle, 1, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, size_per_chan, &samples_read, NULL) );
		vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)samples_read, total_sofar + samples_read);
		if (samples_read != 1){  /* sanity check. */
			feprintf ("This shouldn't be possible: blocking read of 1 sample with infinite timeout returned %d samples, expected 1.\n", (int)samples_read);
		}
	}
	return (samples_read);
}


/* Do it... */
int main(int argc, char* argv[]){

	int	opt; extern char *optarg; extern int optind, opterr, optopt;       /* getopt */
	char   *input_channel = DEV_DEV"/"INPUT_CHANNEL;
	uInt64	num_samples = 0, total_samples = 0;  	/* num_samples is how many (per channel) we want. total_samples is how many we have so far */
	int32   samples_read;				/* samples_read is how many we just got this time. */
	int     n1 = -1, n2_abs = -1, nfail = -1, n2;	/* nth sample (starting at 0) for Edge1, Edge2 or Square-wave failure. n2_abs is sample-num, n2 is (n2_abs - n1) */
	double  t1_ns, t2_ns, tfail_ns;			/* times of these events. t2 is delta from n1 to n2_abs */
	float64 sample_rate_req = 0, sample_rate_actual = 0;	/* sample_rate_req is what we requested, sample_rate_actual is the coerced (actual) value */
	int     allow_overwrite = 0, write_outfile = 0, i, exit_status;
	int     logic_level = LOGIC_UNKNOWN, logic_level_prev = LOGIC_UNKNOWN; 	/* initialise to -1, to be invalid */
	float64 data[BUFFER_SIZE];		/* Sample data-buffer.*/
	char   *output_filename = NULL;
	FILE   *outfile = stdout;
	struct  stat stat_p;
	enum    mode { none, measure, check }; enum mode mode=none;

	/* Parse options and check for validity */
        if ((argc > 1) && (!strcmp (argv[1], "--help"))) {      /* Support --help, without the full getopt_long */
                print_help(argv[0]);
                exit (EXIT_SUCCESS);
        }

        while ((opt = getopt(argc, argv, "dhxf:m:n:")) != -1) {  /* Getopt */
                switch (opt) {
                        case 'h':                               /* Help */
				print_help(argv[0]);
				exit (EXIT_SUCCESS);
				break;
			case 'd':				/* Debugging */
				debug = 1;
				break;
			case 'x':				/* Allow overwrite */
				allow_overwrite = 1;
				break;
			case 'f':				/* Sample Frequency. Float64 */
				sample_rate_req = strtod(optarg, NULL);
				if ( (sample_rate_req < DEV_FREQ_MIN) || (sample_rate_req > DEV_FREQ_MAX) ){  /* Limits of device */
					feprintf ("Fatal Error: sample rate must be between %f and %f Hz.\n", DEV_FREQ_MIN, DEV_FREQ_MAX);
				}
				break;
			case 'n':				/* Number of samples to capture. Integer.  */
				num_samples = strtoll (optarg, NULL, 10);
				if (num_samples <= 0){
					feprintf ("Fatal Error: number of samples must be > 0.\n");
				}else if (num_samples < DEV_SAMPLES_MIN){
					feprintf ("Fatal Error: number of samples must be >= %d.\n", DEV_SAMPLES_MIN);
				}else if (num_samples > (DEV_SAMPLES_MAX) ){  /* Too many samples for finite mode. */
					feprintf ("Fatal Error: number of samples must be <= %d. [Use continuous reading mode instead.]\n", DEV_SAMPLES_MAX);
				}
				break;
			case 'm':				/* Mode: "measure" or "check" */
				if (!strcasecmp(optarg, "measure")){
					mode = measure;
				}else if (!strcasecmp(optarg, "check")){
					mode = check;
				}else{
					feprintf ("Unrecognised mode, %s.\n", optarg);
				}
				break;
			default:
				feprintf ("Unrecognised argument %c. Use -h for help.\n", opt);
				break;
		}
	}

	if (argc - optind >= 1){				/* Optional output file. May be "-" for stdout. */
		output_filename = argv[optind];
		write_outfile = 1;

		if (!strcmp(output_filename, "-")){
			outfile = stdout;
		}else if ( (!allow_overwrite) && (strcmp(output_filename, "/dev/null")) && (stat (output_filename, &stat_p) != -1) ){	/* check we can't stat, i.e. doesn't exist. */
			feprintf ("Output file '%s' already exists, and -x was not specified. Will not overwrite.\n", output_filename);
		}else if ( ( outfile = fopen ( output_filename, "w" ) ) == NULL ) {   /* open for writing and truncate */
			feprintf ("Could not open %s for writing: %s\n", output_filename, strerror ( errno ) );
		}
	}

	if (num_samples == 0){  /* Check for required args */
		feprintf ("Number of samples must be specified, with -n NUM.\n");
	}else if (sample_rate_req == 0){
		feprintf ("Sample frequency must be specified, with -f FREQ.\n");
	}else if (mode == none){
		feprintf ("Mode must be specified, with -m mode.\n");
	}

	/* Create, Configure and Commit the DAQmx task */
	create_configure_commit_task(input_channel, num_samples, sample_rate_req, &sample_rate_actual);

	/* Write out header to file (use the readback values where they might differ from the requested ones). */
	if (write_outfile){
		outprintf ("#Data from %s:\n", DEV_NAME);		outprintf ("#freq_hz:  %.6f\n", sample_rate_actual);		outprintf ("#samples:  %lu\n", (unsigned long)num_samples);
		outprintf ("#channel:  %s\n", INPUT_CHANNEL);   	outprintf ("#voltage:  %.3f\n", VOLTAGE_MAX);		outprintf ("#coupling: %s\n", INPUT_COUPLING_STR);
		outprintf ("#terminal: %s\n", TERMINAL_MODE_STR);	outprintf ("#trigger:  %s\n", TRIGGER_EDGE_STR);	outprintf ("#lf_ear:   %s\n", (ENABLE_ADC_LF_EAR ?"on":"off") );
	}

	/* Ready to go... print a brief summary */
	eprintf ("Acquiring %lu samples, Frequency %f (requested %f), waiting for %s edge trigger on %s.\n", (unsigned long)num_samples, sample_rate_actual, sample_rate_req, TRIGGER_EDGE_STR, DEV_TRIGGER_INPUT);

	/* Start task. */
	start_task();

	/* Loop, acquiring data, reading, processing... */
	while (1){

		/* Read data from device (blocking read, returns at least 1, maybe more) */
		samples_read = blocking_read_1_to_n_samples (data, (sizeof(data)/(sizeof(data[0])*NUM_CHANNELS)), total_samples);

		/* If saving data to file, write it out. */
		if (write_outfile){
			for (i=0; i < samples_read; i++){
				outprintf("%f\n",data[i]);
			}
		}

		if (mode == measure){	/* Measurement mode Expect to see:  ----______---- . */

			for (i=0; i < samples_read; i++){
				logic_level = (data[i] > LOGIC_HIGH_VOLTAGE);

				if ( logic_level == 0 && logic_level_prev == LOGIC_UNKNOWN){  /* First sample is (already) low */
					n1 = 0;
					deprintf ("First value (edge 1) is already low at sample %d.\n", n1);
				}else if ( logic_level == 0 && logic_level_prev == 1){   /* High-low transition */
					if (n1 == -1){	/* count the first time only */
						n1 = (total_samples+i);
						deprintf ("Falling edge (1) occurred at sample %d.\n", n1);
					}else{
						nfail = (total_samples+i);
						eprintf ("Warning: unexpected H->L transition at sample %d. Already happened at %d.\n", nfail, n1);
					}
				}else if ( logic_level == 1 && logic_level_prev == 0){   /* Low-high transition */
					if (n2_abs == -1){
						n2_abs = (total_samples+i);
						deprintf ("Rising edge (2) occurred at sample %d.\n", n2_abs);
					}else{
						nfail = (total_samples+i);
						eprintf ("Warning: unexpected L->H transition at sample %d. Already happened at %d.\n", nfail, n2_abs);
					}
				}
				logic_level_prev = logic_level;
			}

		}else{			/* Check mode. Expect to see a perfect square wave, alternating highs and lows. */

			for (i=0; i < samples_read; i++){
				logic_level = (data[i] > LOGIC_HIGH_VOLTAGE);

				if (logic_level == logic_level_prev){  /* Same value, repeated. This is the failure point. */
					nfail = (total_samples+i);
					deprintf ("Square-wave failure! Repeated level is %d, Failed at sample %d\n", logic_level, nfail);
					break;
				}
				logic_level_prev = logic_level;
			}
		}

		total_samples += samples_read;

		/* Have we now got all the samples we need? */
		if ( total_samples >= num_samples) {  	/* '>=' is for safety; '==' is correct. */
			deprintf ("Finished acquiring all %lld samples...breaking out of loop.\n", num_samples);
			break;
		}
	}

	/* Stop, Clear task. */
	stop_clear_clean_task();

	/* Summarise */
	if ( mode == measure ){		/* Measurement mode */
		n2 = n2_abs - n1;
		t1_ns = n1 * 1e9 / sample_rate_actual;
		t2_ns = n2 * 1e9 / sample_rate_actual;

		if (n1 != -1 && n2_abs != -1 && nfail == -1){	/* We measured exactly one of each transition */

printf ("FIXME\n");
//what if n1 == 0?
 //this EITHER means we are perfectly tuned, OR it means we are premature.  FIXME!! Use ref-trigger to distinguish them.



			eprintf ("Measurement succeeded: 1 of each transition. Delay = %.6f seconds, Length = %.6f seconds\n", t1_ns*1e9, t2_ns*1e9);
			eprintf ("#nominal_freq\tcoerced_freq\tn1_samples\tn2_samples\tt1_ns\t\tt2_ns\n");
			printf  ("%f\t%f\t%d\t\t%d\t\t%f\t%f\n", sample_rate_req, sample_rate_actual, n1, n2, t1_ns, t2_ns);
			exit_status = EXIT_SUCCESS;

		}else if (n1 != -1 && n2_abs != -1 && nfail == -1){	/* We got the transitions, but also some spurious edges */
			eprintf ("Measurement doubtful: more than 1 of each transition (last duplicate at %d).  Delay = %.6f seconds, Length = %.6f seconds\n", nfail, t1_ns*1e9, t2_ns*1e9);
			eprintf ("#nominal_freq\tcoerced_freq\tn1_samples\tn2_samples\tt1_ns\t\tt2_ns\n");
			printf  ("%f\t%f\t%d\t\t%d\t\t%f\t%f\n", sample_rate_req, sample_rate_actual, n1, n2, t1_ns, t2_ns);
			exit_status = EXIT_FAILURE;

		}else{						/* We missed at least one of the edges we expected */
			eprintf ("Measurement failed: at least one edge was missed. Delay = %.6f seconds, Length = %.6f seconds\n", t1_ns*1e9, t2_ns*1e9);
			eprintf ("#nominal_freq\tcoerced_freq\tn1_samples\tn2_samples\tt1_ns\t\tt2_ns\n");
			printf  ("%f\t%f\t%d\t\t%d\t\t%f\t%f\n", sample_rate_req, sample_rate_actual, n1, n2, t1_ns, t2_ns);
			exit_status = EXIT_FAILURE;
		}

	}else{				/* Check mode */
		tfail_ns = nfail * 1e9 / sample_rate_actual;
		if (nfail == -1){				/* Every sample got a square-wave transition. In Perfect Sync :-) */
			eprintf ("Check succeeded: we remained synchronised for all %lld samples (%.6f seconds).\n", total_samples, total_samples / sample_rate_actual);
			eprintf ("#nominal_freq\tcoerced_freq\tn_fail\tfail_type\n");
			printf  ("%f\t%f\t%d\t%d\n", sample_rate_req, sample_rate_actual, nfail, -1);
			exit_status = EXIT_SUCCESS;

		}else{						/* We saw a duplicate level */
			eprintf ("Check failed early: we lost sync with a duplicate logic %s, after %d samples (%.6f seconds).\n", (logic_level?"high":"low"), nfail, nfail / sample_rate_actual);
			eprintf ("#nominal_freq\tcoerced_freq\tn_fail\tfail_type\n");
			printf  ("%f\t%f\t%d\t%d\n", sample_rate_req, sample_rate_actual, nfail, logic_level);
			exit_status = EXIT_FAILURE;
		}
	}

	if (write_outfile){
		fclose ( outfile );
	}
	return (exit_status);
}


//FIXME: should also cleanup lockfiles on receiving Ctrl-C: c.f. ni4462_test.c

