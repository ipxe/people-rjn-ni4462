/* Capture program for the IR camera. This loops indefinitely: wait for trigger; capture N data points, do linear regression, print stats, ....
   For parameter experimentation, look at ni4462_test.

 * Copyright (C) Richard Neill 2011-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software, You can
 * redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.  An exception is granted to link against 
 * the National Instruments proprietary libraries, such as libnidaqmx. There is NO WARRANTY, neither express nor implied.
 * For the details, please see: http://www.gnu.org/licenses/gpl.html
*/

/* Headers */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <libgen.h>

#ifdef  USE_DUMMY_LIBDAQMX						/* Option to compile in dummy mode, without actually using the real libnidaqm */
  #include "daqmx_dummy.c"
#else
  #include <NIDAQmx.h>							/* NI's library. Also '-lnidaqmx' */
#endif

#define LIBDAQMX_TMPDIR		"/tmp/natinst/"				/* Temp dir for NI's lock files. We clean this up below. Caution. */
#ifndef OnboardClock							/* Bugfix: defined in docs, not in header */
#define OnboardClock NULL
#endif


/* Device properties */
#define DEV_NAME			"NI4462"			/* Product name */
#define DEV_DEV				"Dev1"				/* Which PCI device to use. */
#define DEV_TRIGGER_INPUT		"PFI0"				/* Name of the Digital trigger input */
#define DEV_NUM_CH			4				/* Number of input channels on this one. Avoids (some of the) hardcoded "4"s. */
#define DEV_FREQ_MIN			32.0				/*    Min freq (Hz) */
#define DEV_FREQ_MAX			204800.0			/*    Max freq (Hz) */
#define DEV_FREQ_QUANTISATION		"181.9 uS/s"			/* Frequency quantisation, to nearest micro sample-per-second */
#define DEV_SAMPLES_MIN			2				/* Min number of samples at a time. (experimentally) */
#define DEV_SAMPLES_MAX			16777215			/* 2^24 - 1, measured experimentally */
#define DEV_PRETRIGGER_SAMPLES_MIN	2				/* Min number of samples before trigger, in reference-trigger mode */
#define DEV_ADC_FILTER_DELAY_SAMPLES	63				/* ADC filter standard delay. (But depends on sample freq if Low-Frequency Alias Rejection Enabled) */
#define RTSI6				"RTSI6"				/* Sample clock connection to Pulseblaster */

/* CONFIGURATION CHOICES. */						/* (Experiment with ni4462_test.c, if desired, then set here. */
#define INPUT_CHANNELS			"ai0:3"				/* All channels. Options: ai0, ai1, ai2, ai3, ai0:3  */

#define VOLTAGE_RANGE_0			0.316				/* Options: 0.316, 1, 3.16, 10, 31.6, 100.  x means [-x,+x]. */
#define VOLTAGE_RANGE_1			1.0
#define VOLTAGE_RANGE_2			3.16
#define VOLTAGE_RANGE_3			10.0

#define INPUT_COUPLING			DAQmx_Val_DC			/* Options: DAQmx_Val_DC, DAQmx_Val_AC */
#define INPUT_COUPLING_STR		"dc"				/* NB: if AC, remember the settling time AFTER taskCommit: see manual, section: Analog Input Channel Configurations -> Input Coupling  */

#define TERMINAL_MODE			DAQmx_Val_Diff			/* Options: DAQmx_Val_Diff, DAQmx_Val_PseudoDiff */
#define TERMINAL_MODE_STR		"differential"

#define TRIGGER_EDGE			DAQmx_Val_Falling		/* Options: DAQmx_Val_Rising, DAQmx_Val_Falling */
#define TRIGGER_EDGE_STR		"falling"

#define ENABLE_ADC_LF_EAR		0				/* Bool: Disable low frequency enhanced alias rejection (ensure filter-delay is always exactly 63 samples) */
#define INT_CLOCK_EDGE  		DAQmx_Val_Rising		/* Sample on the RE of the Internal clock. (Probably doesn't matter). */
#define INT_CLOCK_EDGE_STR  		"rising"

#define PRETRIGGER_SAMPLES		DEV_PRETRIGGER_SAMPLES_MIN	/* Pretriggering is required to make RTSI6 start clocking and enable the Pulseblaster's Gated Hw_Trigger */
#define TRIGGER_EARLY_BY 		(PRETRIGGER_SAMPLES + DEV_ADC_FILTER_DELAY_SAMPLES)  /* Trigger is effectively this much "back in time". This is the delay needed. */

#define MISSED_TRIGGER_DETECT		1.3				/* Threshold to detect missed trigger: frame intervals differ by more than 30% */
#define SLOW_TASKLOOP_DETECT_MS		2.0				/* Threshold to warn if the taskloop is too slow. Typically takes 0.75ms.  */

/* Default values */
#define DEFAULT_SAMPLE_HZ		204800				/* Default to 204.8 kHz. (max speed) */
#define DEFAULT_COUNT			1000				/* Default to 1000 samples (per frame) */
#define DEFAULT_MAXFRAMES		-1				/* Max number of frames before exit. -1 means loop forever. */
#define DEFAULT_GROUP_SIZE		1				/* Frames in a group (triggered once) */
#define DEFAULT_GROUP_INTERVAL		0				/* Number of samples in the frame-interval */
#define DEFAULT_VOLTAGE_RANGE		VOLTAGE_RANGE_0			/* -...+ swing. Set gain highest. */
#define DEFAULT_GUARD_PRE     		1				/* Guard samples, to discard from the start of the dataset */
#define DEFAULT_GUARD_POST    	 	1				/* ... from the end ... */
#define DEFAULT_GUARD_INTERNAL 		1				/* ... internal (between samples) for imaging modes */
#define DEFAULT_NUM_CDSM  		10				/* Default number for CDS_Multiple. */

/* Buffer sizes */
#define BUFFER_SIZE_TUPLES		25000				/* Max number of data tuples read at a time.  Experimentally, can  be as small as 10, performance is limited below 400; since RAM is plentiful let's say 25k. */
#define BUFFER_SIZE			(BUFFER_SIZE_TUPLES * DEV_NUM_CH) /* Size of the data buffer for all channels (4 channels wide) */


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
int terminate_loop = 0;		/* for Ctrl-C */
char *state = "Initialising";
TaskHandle taskHandle = 0;


/* Show help */
void print_help(char *argv0){
	argv0 = basename(argv0);
	eprintf("\nINTRO: %s captures and pre-processes data from the National Instruments %s PCI device. Various modes are\n"
		"supported, notably linear_regression and differential_imaging. The device is configured in %s-coupled %s-mode, with\n"
		"a %s edge trigger. Trigger-compensation is %d samples.\n"
		"\n"
		"USAGE:  %s  [OPTIONS]  \n"
		"   -h                print help and exit\n"
		"   -d                debug: be much more verbose. Also, make warnings fatal.\n"
		"   -r                dump (prefixed) raw data in output. Prefixed '#='. Guard samples are not skipped here.\n"
		"   -a   ANALYSIS     analysis mode: Options: raw, lin_reg, cds_multiple, image, image_diff. [default: lin_reg].\n"
		"   -f   FREQ         sample frequency (Hz). [default: %d].\n"
		"   -v   VOLTAGE      set the voltage range (V). [-v_limit, +v_limit]. [Values: %4.2f, %4.2f, %4.2f, %4.2f; default: %4.2f].\n"
		"   -n   NUM          number of samples per frame. [default: %d].\n"
		"   -m   MAX_FRAMES   maximum number of frames. ('cont' for unlimited). [default: %d].\n"
		"   -g   GROUPSIZE    group frames with a single trigger per group (reduces task restart-latency). [default: %d]\n"
		"   -i   INTERVAL     interval between frames of a given group. (number of samples to skip). [default: %d]\n"
		"   -x   GUARD_PRE    number of \"guard\" samples to discard from the start of the frame's data. [default: %d].\n"
		"   -y   GUARD_POST   number to discard from the frame's end. (-x,-y,-z are counted *within* -n NUM). [default: %d].\n"
		"   -z   GUARD_INT    number of internal guard samples, between each pixel, in the imaging modes. [default: %d].\n"
		"   -c   NUM_CDS      cds_multiple: number of samples to use for averaging in each side of the CDS_m. [default: %d].\n"
		"   -p   PIXELS       image/image_diff: number of pixels (per quadrant). [used as a check on -n,-x,-y,-z].\n"  /* -p is redundant. but required to ensure the operator really understands the maths. */
		"   -T   TRIGFILE     When ready for first ext-trigger, delete this (pre-created) empty file. Other processes can inotifywait().\n"
		"\n"
		"The program takes -n samples (on all channels) in each frame; -m frames in total. Of these n samples, the first -x,\n"
		"and last -y may be considered \"guard\"-samples and are discarded, (as are internal guards -z in the imaging modes).\n"
		"Each (ungrouped) frame is independently triggered, the program exits after -m frames (or runs continuously).\n\n"
		"Because the %s is so slow (~ 1ms) at taskStop...taskStart, we cannot re-trigger quickly. So frames may be grouped (-g) into\n"
		"a single task, following one another immediately (or skipping -i samples). This avoids the overhead of the task model, but\n"
		"sacrifices the option of resynchronisation with a trigger pulse. [As the master clock is shared; dead-reckoning is ok.]\n\n"
		"The sample-frequency and voltage-gain may also be set; overflows in the analog or digital domains are detected.\n"
		"The analyis modes (-a) are:\n"
		"\n"
		"   * RAW mode        : N samples are taken in each frame, and printed. Statistics are also calculated.\n"
		"   * LIN_REG mode    : In each frame, the gradient is estimated by OLS regression.\n"
		"   * CDS_M mode      : In each frame, the gradient is estimated by Correlated double-sampling with -c multiple reads.\n"
		"   * IMAGE mode      : An 'image' (of -p pixels) is sampled, discarding internal guards. See dat2cam/cam2tiff.\n"
		"   * IMAGE_DIFF mode : The images from alternate frames are subtracted (even_frame - odd_frame) and output.\n"
		"\n"
		"SYNCHRONISATION : The %s trigger input is controlled by the PulseBlaster via a DelayLine. The PulseBlaster's own \n"
		"                   HW_Trigger is gated by the %s's %s output; so the %s must be in \'reference-trigger\' mode.\n"
		"COMPENSATION    : Triggering looks \"back in time\", compensate by setting the DelayLine to exactly %d sample-periods.\n"  /* i.e. (TRIGGER_EARLY_BY * sample_interval) */
		"OUTPUTS         : Stdout receives headers (prefixed '#') and parseable data (tab/newline-delimited). Messages to Stderr.\n"
		"CONTROL         : Sending Ctrl-C cleanly breaks out of the frame at its end; Ctrl-\\ terminates immediately. SigUSR1 prints state.\n"
		"MISSED TRIGGERS : A missed-trigger is inferred if the interval between two frames varies by more than a factor than %.3g.\n"  /* Can't truly detect missed trigger pulses; consistency checking is the best we can do. */
		"TASK OVERHEAD   : The overhead for taskStop...taskStart is checked. Warning if it exceeds %.3g ms.\n"
		"SEE ALSO        : ni4462_test, pb_ni4462_trigger, arduino_delay, dat2cam\n"
		"\n"
		,argv0, DEV_NAME, INPUT_COUPLING_STR, TERMINAL_MODE_STR, TRIGGER_EDGE_STR, TRIGGER_EARLY_BY,
		 argv0, DEFAULT_SAMPLE_HZ, VOLTAGE_RANGE_0, VOLTAGE_RANGE_1, VOLTAGE_RANGE_2, VOLTAGE_RANGE_3, DEFAULT_VOLTAGE_RANGE, DEFAULT_COUNT, DEFAULT_MAXFRAMES, DEFAULT_GROUP_SIZE, DEFAULT_GROUP_INTERVAL,
		 DEFAULT_GUARD_PRE, DEFAULT_GUARD_POST, DEFAULT_GUARD_INTERNAL, DEFAULT_NUM_CDSM, DEV_NAME,
		 DEV_TRIGGER_INPUT, DEV_NAME, RTSI6, DEV_NAME, TRIGGER_EARLY_BY, MISSED_TRIGGER_DETECT, SLOW_TASKLOOP_DETECT_MS);
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
			DAQmxStopTask(taskHandle);
			DAQmxClearTask(taskHandle);
		}
		feprintf ("DAQmx Fatal Error (%d): %s\n\n%s\n\n", error, error_buf, error_buf2);
	}else if (debug){	 /* Warning: but in debug mode, so be fatal */
		if( taskHandle!=0 ) {		/* Stop task, if there is one. */
			DAQmxStopTask(taskHandle);
			DAQmxClearTask(taskHandle);
		}
		feprintf ("DAQmx Warning (%d), with debug (-d), will exit. Error: %s\n\n%s\n\n", error, error_buf, error_buf2);
	}else{			/* Just a warning: print it and continue. */
		eprintf ("DAQmx Warning: %s\n\n%s\n\n", error_buf, error_buf2);
	}
	return;
}

/* Correct the timestamp to account for pretrigger samples and trigger latency. We're already compensating in hardware (arduino_delay) for the underlying
 * 65 samples (TRIGGER_EARLY_BY) of latency (pre-triggering + digital filter latency. So the NI is triggered late, but captures earlier in time. Subtract
 * this delay from the timestamp to obtain the true timestamp of the actual data. */
double correct_timestamp (struct timeval timestamp, double sample_interval){
	return ( (timestamp.tv_sec + (timestamp.tv_usec*1e-6)) -  (TRIGGER_EARLY_BY * sample_interval) );
}

/* Return the difference of two timestamps, now - then, (in seconds, as double) */
double timestamp_diff (struct timeval now, struct timeval then){
	return (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec));
}

/* Add in quadrature (for standard deviations) */
double quadrature_add2 (double a, double b){
	return (sqrt(fabs( a*a + b*b )));
}
double quadrature_add4 (double a, double b, double c, double d){
	return (sqrt(fabs( a*a + b*b +c*c + d*d)));
}


/* Signal handler: handle Ctrl-C in middle of main loop. */
void handle_signal_cc(int signum){
	eprintf ("Ctrl-C (sig %d), stopping at the end of this (complete) frame. (Use Ctrl-\\ to kill now).\n", signum);
	state = "Stopping";
	terminate_loop = 1; 	/* Break out of the while loop this time round,. */
}

/* Signal handler: handle SIGUSR1: print current state. */
void handle_signal_usr1(int signum __attribute__ ((unused)) ){
	eprintf ("%s\n", state);  //global.
}


/* Do it... */
int main(int argc, char* argv[]){

	int	opt; extern char *optarg; extern int optind, opterr, optopt;       /* getopt */
	char   *input_channels = DEV_DEV"/"INPUT_CHANNELS;
	uInt64	num_samples_per_frame = DEFAULT_COUNT;
	uInt64	num_samples_per_group;
	float64 sample_rate = DEFAULT_SAMPLE_HZ;
	float64 sample_interval = ((double)1 / DEFAULT_SAMPLE_HZ);
	float64 vin_max = DEFAULT_VOLTAGE_RANGE;
	int     num_frames = DEFAULT_MAXFRAMES;
	int     group_size = DEFAULT_GROUP_SIZE;
	int     group_interval = DEFAULT_GROUP_INTERVAL;
	int     guard_pre = DEFAULT_GUARD_PRE;
	int	guard_post = DEFAULT_GUARD_POST;
	int     guard_internal = DEFAULT_GUARD_INTERNAL;
	int 	num_cdsm = DEFAULT_NUM_CDSM;
	int     num_pixels = 0;
	int32   he_retval = 0;   		/* Used by #define handleErr() above */
	float64 readback_hz = 0, readback_v1 = 0, readback_v2 = 0, readback_g;
	bool32  overload_occurred = 0;
	int32   samples_read_thistime;		/* Number of samples (per channel) that were actually read in this pass */
	uInt64  samples_read_inner = 0;		/* Number of samples (per channel) that have been read in the inner loop */
	uInt64  samples_read_total = 0;		/* Number of samples (per channel) that have been read so far in (grand) total */
	uInt64	n, n_this, n_discard;
	int     i, c, ret, px, guard, do_break, opt_c = 0, opt_i = 0, opt_p = 0, dump_raw = 0, prev_frame, frame = 0, group = 0, missed_trigger = 0, group_pos = 0, do_triggerready_delete = 0;
	float64 data[BUFFER_SIZE];		/* Our read data buffer. Multiple of 4. Needn't have room for num_samples_per_frame all at once */
	float64 S_x[DEV_NUM_CH], S_y[DEV_NUM_CH], S_xx[DEV_NUM_CH], S_yy[DEV_NUM_CH], S_xy[DEV_NUM_CH], S_y_g1[DEV_NUM_CH], S_yy_g1[DEV_NUM_CH], S_y_g2[DEV_NUM_CH], S_yy_g2[DEV_NUM_CH];
	float64 b[DEV_NUM_CH], a[DEV_NUM_CH], s[DEV_NUM_CH], se_a[DEV_NUM_CH], se_b[DEV_NUM_CH], r[DEV_NUM_CH], b_Dx[DEV_NUM_CH];
	float64 mean[DEV_NUM_CH], stdev[DEV_NUM_CH], min[DEV_NUM_CH], max[DEV_NUM_CH], D_cds[DEV_NUM_CH], stdev_cds_g1[DEV_NUM_CH], stdev_cds_g2[DEV_NUM_CH], se_b_cds[DEV_NUM_CH];
	double  first_trigger_interval = 0, this_trigger_interval = 0, stopstart_interval = 0;
	float64 *pixels1[DEV_NUM_CH], *pixels2[DEV_NUM_CH], *raw[DEV_NUM_CH], **pixels; /* Pointer to an array of pixel data. Malloc()d later, once we know the size. */
	FILE    *outfile = stdout;
	char    error_buf[2048]="\0";
	struct  stat stat_p;     		/* pointer to stat structure */
	char   *triggerready_filename = "";	/* trigger_ready filename */
	char   *mode_arg="lin_reg";
	struct  timeval	frame_start, frame_end, group_end, group_end_prev, task_prestop, task_started;
	enum    mode { RAW, LINREG, CDS_M, IMAGE, IMAGE_CDS }; enum mode mode=LINREG;  /* Which mode to operate in? */

	/* Set handler for SIGUSR1: print state to stderr. */
	signal(SIGUSR1, handle_signal_usr1);

	/* Parse options and check for validity */
        if ((argc > 1) && (!strcmp (argv[1], "--help"))) {      /* Support --help, without the full getopt_long */
                print_help(argv[0]);
                exit (EXIT_SUCCESS);
        }

        while ((opt = getopt(argc, argv, "dhra:c:f:g:i:n:m:p:v:x:y:z:T:")) != -1) {  /* Getopt */
                switch (opt) {
			case 'a':				/* Analysis type */
				mode_arg = optarg;
				if (!strcasecmp(optarg, "raw")){
					mode = RAW;
				}else if ((!strcasecmp(optarg, "lin_reg")) || (!strcasecmp(optarg, "linreg"))) {
					mode = LINREG;
				}else if (!strcasecmp(optarg, "cds_multiple")){
					mode = CDS_M;
				}else if (!strcasecmp(optarg, "image")){
					mode = IMAGE;
				}else if (!strcasecmp(optarg, "image_diff")){
					mode = IMAGE_CDS;
				}else{
					feprintf ("Illegal mode. Values of -a can be: raw, lin_reg, cds_multiple, image, image_diff.\n");
				}
				break;

			case 'c':				/* Number of CDS_M multiple reads in each half */
				opt_c = 1;
				num_cdsm = atoi(optarg);
				break;

			case 'd':				/* Debugging */
				debug = 1;
				break;

			case 'f':				/* Sample Frequency. Float64 */
				sample_rate = strtod(optarg, NULL);
				if ( (sample_rate < DEV_FREQ_MIN) || (sample_rate > DEV_FREQ_MAX) ){  /* Limits of device */
					feprintf ("Fatal Error: sample rate must be between %f and %f Hz.\n", DEV_FREQ_MIN, DEV_FREQ_MAX);
				}
				sample_interval = ((double)1 / sample_rate);
				break;

			case 'g':				/* Group of frames */
				group_size = atoi(optarg);
				if (group_size <= 0){
					feprintf ("Fatal Error: group_size (-g) must be > 0.\n");
				}
				break;

			case 'h':                               /* Help */
				print_help(argv[0]);
				exit (EXIT_SUCCESS);
				break;

			case 'i': 				/* Interval between frames */
				group_interval = atoi(optarg);
				if (group_interval < 0){
					feprintf ("Fatal Error: group_interval (-i) must be >= 0.\n");
				}
				break;

			case 'm':				/* Max frames. "cont" for continuous */
				if (!strcasecmp(optarg, "cont")){
					num_frames = -1;
				}else{
					num_frames = atoi (optarg);
					if ( (num_frames <= -1) || (num_frames == 0) ){
						feprintf ("Fatal Error: max frames must be > 0, (or 'cont' for continuous).\n");
					}
				}
				break;

			case 'n':				/* Number of samples to capture. Integer.  */
				num_samples_per_frame = strtoll (optarg, NULL, 10);
				if (num_samples_per_frame <= 0){
					feprintf ("Fatal Error: number of samples per frame must be > 0.\n");
				}else if (num_samples_per_frame < DEV_SAMPLES_MIN){
					feprintf ("Fatal Error: number of samples per frame must be >= %d.\n", DEV_SAMPLES_MIN);
				}else if (num_samples_per_frame > (DEV_SAMPLES_MAX) ){  /* Too many samples for finite mode. */
					feprintf ("Fatal Error: number of samples per frame must be <= %d. [Recompile using continuous reading instead, see ni4462_test.c].\n", DEV_SAMPLES_MAX);
				}
				break;

			case 'p':				/* Number of pixels (per quadrant) in imaging mode */
				opt_p = 1;
				num_pixels = atoi(optarg);
				if (num_pixels <= 0){
					feprintf ("Fatal Error: num_pixels (-p) must be > 0.\n");
				}
				break;

			case 'r':				/* Raw output: print raw data too */
				dump_raw = 1;
				break;

			case 'v':				/* Voltage Limit: set gain/rainge for input voltage swing of [-v_limit, +v_limit] */
				vin_max = atof(optarg);
				if ( (vin_max != VOLTAGE_RANGE_0) && (vin_max != VOLTAGE_RANGE_0) && (vin_max != VOLTAGE_RANGE_2) && (vin_max != VOLTAGE_RANGE_3) ){
					feprintf ("Fatal Error: voltage range must be set to +/-V where V is in { %4.2f, %4.2f, %4.2f }\n", VOLTAGE_RANGE_0, VOLTAGE_RANGE_1, VOLTAGE_RANGE_2);
				}
				break;

			case 'x':				/* Guard samples (pre) */
				guard_pre = atoi(optarg);
				if (guard_pre < 0){
					feprintf ("Fatal Error: guard_pre (-x) must be >= 0.\n");
				}
				break;

			case 'y':				/* Guard samples (post) */
				guard_post = atoi(optarg);
				if (guard_post < 0){
					feprintf ("Fatal Error: guard_post (-y) must be >= 0.\n");
				}
				break;

			case 'z':				/* Number internal guard samples in imaging mode */
				opt_i = 1;
				guard_internal = atoi(optarg);
				if (guard_internal < 0){
					feprintf ("Fatal Error: guard_internal (-z) must be >= 0.\n");
				}
				break;
				
			case  'T':				/* This file is pre-created; we delete it when we are ready for trigger. Other process inotifywait()s. */
				do_triggerready_delete = 1;
				triggerready_filename = optarg;
				break;
				
			default:
				feprintf ("Unrecognised argument %c. Use -h for help.\n", opt);
				break;
		}
	}


	/* Calculations */
	num_samples_per_group = (num_samples_per_frame * group_size)  +  ( group_interval * (group_size -1) );

	/* Sanity checks */
	if (argc - optind != 0){
		feprintf ("This takes no non-option arguments. Use -h for help.\n");
	}
	if (do_triggerready_delete){		/* Trigger Ready file must pre-exist, and be empty. */
 		if (stat (triggerready_filename, &stat_p) == -1){
 			feprintf ("Trigger-Ready signal-file '%s' doesn't exist. It must be pre-created (empty) by the external process and supplied to us.\n", triggerready_filename);
 		}else if (stat_p.st_size > 0){
 			feprintf ("Trigger-Ready signal-file '%s' isn't empty. Will not accidentally delete something containing data.\n", triggerready_filename);
 		}
 	}
	if ( num_samples_per_frame < (unsigned int)(guard_pre + guard_post + DEV_ADC_FILTER_DELAY_SAMPLES + PRETRIGGER_SAMPLES) ){
		feprintf ("Error: not enough samples. N must exceed guard_pre + guard_post + FILTER_DELAY + PRETRIGGER. Current values are: %lld, %d, %d, %d, %d\n", (long long)num_samples_per_frame, guard_pre, guard_post, DEV_ADC_FILTER_DELAY_SAMPLES, PRETRIGGER_SAMPLES);
	}
	if (num_samples_per_frame - (guard_pre + guard_post) < 3){
		feprintf ("Error: number of samples per frame (excluding guard_pre/guard_post) must be 3 or more. Otherwise, the linear-regression statistics can't be calculated.\n");
	}
	if ((num_frames != -1) && (num_frames % group_size != 0)){
		feprintf ("Error: finite number of frames (%d) must be an exact multiple of the group size (%d)\n", num_frames, group_size);
	}
	if (num_samples_per_group  > DEV_SAMPLES_MAX){
		feprintf ("Error: too many samples per group. Value %lld exceeds max number of samples per Task, %d. [Calculate: samples * groups + interval * (groups-1) ].\n", (long long)num_samples_per_group, DEV_SAMPLES_MAX);
	}
	if (mode != CDS_M && opt_c){
		feprintf ("Error: option -c specified without setting mode to 'cds_multiple'.\n");
	}
	if ((mode == CDS_M) && (num_samples_per_frame - guard_pre - guard_post < (unsigned int)(2 * num_cdsm))) {
		feprintf ("Error: number of samples per frame, excluding guard_pre/guard_post must (obviously) be at least 2* number of multiple-reads.\n");
	}
	if (mode == CDS_M && num_cdsm == 1){
		eprintf ("Warning: CDS with multiple reads, with M = 1: variances will be NANs\n");
	}
	if (mode != IMAGE && mode != IMAGE_CDS && opt_i){
		feprintf ("Error: option -i specified without setting mode to 'image' or 'image_diff'.\n");
	}
	if ((mode == IMAGE || mode == IMAGE_CDS) && !opt_p){
		feprintf ("Error: option -p required in mode 'image' or 'image_diff'.\n");
	}
	if (((mode == IMAGE) || (mode == IMAGE_CDS)) && ((unsigned int)(guard_pre + num_pixels + (num_pixels - 1) * guard_internal + guard_post) != num_samples_per_frame) ){
		feprintf ("Error: in imaging mode, must satisfy: number_of_samples_per_frame = guard_pre + num_pixels + ((num_pixels - 1) * guard_internal) + guard_post.\nCurrent values: samples_per_frame=%d, guard_pre=%d, guard_internal=%d, guard_post=%d, num_pixels=%d.\n", (int)num_samples_per_frame, guard_pre, guard_internal, guard_post, num_pixels);
	}
	if ((mode == IMAGE_CDS) && (num_frames % 2 != 0)){
		feprintf ("Error: in differential imaging mode, number of frames must (obviously) be even.\n");
	}

	 /* keep compiler happy: these initialisations aren't needed, but allow us to use -Wall without noise. */
	gettimeofday(&group_end_prev, NULL); gettimeofday(&task_prestop, NULL);
 	for (c=0; c < DEV_NUM_CH; c++){
  		S_x[c] =  S_xx[c] = S_y [c] = S_yy[c] = S_xy[c] = S_y_g1[c] = S_yy_g1[c] = S_y_g2[c] = S_yy_g2[c] = mean[c] = stdev[c] = 0;  min[c] = 1e10; max[c] = -1e10;
	}

	/* Allocate memory for the pixel arrys or raw data */
	if ( (mode == IMAGE) || (mode || IMAGE_CDS) ){
		for (c=0; c < DEV_NUM_CH; c++){
			pixels1[c] = malloc (num_pixels * sizeof (*pixels1[0]) );
			if (NULL == pixels1[c]){
				feprintf ("Fatal error: couldn't malloc() enough for %d quads of %d pixels.\n", DEV_NUM_CH, num_pixels);
			}
			if (mode == IMAGE_CDS){
				pixels2[c] = malloc (num_pixels * sizeof (*pixels2[0]) );
				if (NULL == pixels2[c]){
					feprintf ("Fatal error: couldn't malloc() enough for %d quads of %d pixels.\n", DEV_NUM_CH, num_pixels);
				}
			}
		}
	}else if (mode == RAW){
		for (c=0; c < DEV_NUM_CH; c++){
			raw[c] = malloc (num_samples_per_frame * sizeof (*raw[0]) );
			if (NULL == raw[c]){
				feprintf ("Fatal error: couldn't malloc() enough for %d quads of %lld samples.\n", DEV_NUM_CH, (long long)num_samples_per_frame);
			}
	 		for (i=0; (unsigned)i < num_samples_per_frame; i++){  /* keep compiler happy, unnecessary initialisation for -Wall */
	 			raw [c][i] = 0;
	 		}
		}
	}

	/* Now create and configure the DAQmx task. See ni4462_test.c for a verbosely commented and debugging example. */

	/* Create Task (name is arbitrary, but avoid "" which sometimes leaks memory) */
	state = "Configuring";
	handleErr( DAQmxCreateTask("Capture",&taskHandle) );

	/* Connect Terminal RTSI6 to the SampleClock. This is essential for the PulseBlaster to trigger: it controls a D-latch on PB's HW_Trigger */
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/ai/SampleClock", "/"DEV_DEV"/"RTSI6,  DAQmx_Val_DoNotInvertPolarity));

	/* Set input channels: choose the channel(s), the terminal-mode, and the voltage-scale. NB the voltage scale is coerced by device capabilities. */
	handleErr( DAQmxCreateAIVoltageChan(taskHandle, input_channels, "AnalogInput", TERMINAL_MODE, -vin_max, vin_max, DAQmx_Val_Volts, NULL) );  /* DAQmx_Val_Volts is the scale-type */
 	handleErr( DAQmxGetAIMin (taskHandle, input_channels, &readback_v1)  ); 	/* Check coercion */
 	handleErr( DAQmxGetAIMax (taskHandle, input_channels, &readback_v2) );
	handleErr( DAQmxGetAIGain(taskHandle, input_channels, &readback_g) );
	deprintf ("Input Voltage range requested: [%f, %f] V; actually coerced by device to: [%f, %f] V. Gain is: %f dB. Terminal_mode: %s.\n", -vin_max, vin_max, readback_v1, readback_v2, readback_g, TERMINAL_MODE_STR);

	/* Configure Channel input-coupling (AC/DC). NB if choosing AC coupling, remember to allow the settling time! */
	deprintf  ("Setting input_coupling to %d, %s ...\n", INPUT_COUPLING, INPUT_COUPLING_STR );
	handleErr( DAQmxSetAICoupling (taskHandle, input_channels, INPUT_COUPLING) );

	/* Configure Triggering. Set trigger input to digital triggering via the external SMA connector, and select the edge. Must use a Reference trigger because we NEED the PulseBlaster to respond to HW_TRIGGER */
	deprintf  ("Setting triggering to external trigger input, %s, using %s edge. Reference trigger with %d pre-trigger samples...\n", DEV_TRIGGER_INPUT, TRIGGER_EDGE_STR, PRETRIGGER_SAMPLES);
	handleErr ( DAQmxCfgDigEdgeRefTrig (taskHandle, DEV_TRIGGER_INPUT, TRIGGER_EDGE, PRETRIGGER_SAMPLES) );

	/* Configure Timing and Sample Count. [Use Rising Edge of Onboard clock (arbitrary choice).] */
	/* Acquire finite number, num_samples_per_group, of samples (on each channel) at a rate of (coereced)sample_rate. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, OnboardClock, sample_rate, INT_CLOCK_EDGE, DAQmx_Val_FiniteSamps, num_samples_per_group ) );  /* Finite number of samples */
	handleErr( DAQmxGetSampClkRate (taskHandle, &readback_hz)  ); 	/* Check coercion */
	deprintf ("Acquiring (finite) %lld samples per task. Sample clock requested: %f Hz; actually coerced to: %f Hz. Using %s edge of the internal sample-clock.\n", (long long)num_samples_per_group, sample_rate, readback_hz, INT_CLOCK_EDGE_STR);

	/* Ensure that DAQmxReadAnalogF64() (below) will not block for completion, but will read all the available samples. */
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, TRUE) );

	/* Disable EnhancedAliasRejectionEnable as #defined above: ensure that filterdelay is constant 63. No benefit at higher frequencies anyway. */
	handleErr( DAQmxSetAIEnhancedAliasRejectionEnable(taskHandle, input_channels, ENABLE_ADC_LF_EAR) );

	/* Commit the task (make sure all hardware is configured and ready) */
	deprintf ("Committing task (%d)\n", DAQmx_Val_Task_Commit);
	state = "Committing";
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) ); //*/   /* Error: DAQmxErrorPALResourceReserved  i.e. -50103  arises if there are two processes contending for access to this device. */
	state = "Committed";

	/* Ready to go... print a brief summary */
	if (mode == LINREG){
		eprintf ("Configuration: Mode: lin_reg,  FreqHz: %f,  Frames: %d,  SampsPerFrame: %ld,  GroupSize: %d,  GuardPre: %d, GuardPost: %d\n", sample_rate, num_frames, (unsigned long)num_samples_per_frame, group_size, guard_pre, guard_post);
	}else if (mode == CDS_M){
		eprintf ("Configuration: Mode: cds_multiple,  FreqHz: %f,  Frames: %d,  SampsPerFrame: %ld,  GroupSize: %d,  Num_CDSm: %d,  GuardPre: %d, GuardPost: %d\n", sample_rate, num_frames, (unsigned long)num_samples_per_frame, group_size, num_cdsm, guard_pre, guard_post);
	}else if (mode == RAW){
		eprintf ("Configuration:Mode: raw,  FreqHz: %f,  Frames: %d,  SampsPerFrame: %ld,  GroupSize: %d,  GuardPre: %d, GuardPost: %d\n", sample_rate, num_frames, (unsigned long)num_samples_per_frame, group_size, guard_pre, guard_post);
	}else if (mode == IMAGE){
		eprintf ("Configuration: Mode: image,  FreqHz: %f,  Frames: %d,  SampsPerFrame: %ld,  GroupSize: %d,  Pixels %d,  GuardPre: %d, GuardPost: %d,  GuardInt: %d\n", sample_rate, num_frames, (unsigned long)num_samples_per_frame, group_size, num_pixels, guard_pre, guard_post, guard_internal);
	}else if (mode == IMAGE_CDS){
		eprintf ("Configuration: Mode: image_diff,  FreqHz: %f,  Frames: %d,  SampsPerFrame: %ld,  GroupSize: %d,  Pixels %d,  GuardPre: %d, GuardPost: %d,  GuardInt: %d\n", sample_rate, num_frames, (unsigned long)num_samples_per_frame, group_size, num_pixels, guard_pre, guard_post, guard_internal);
	}

	/* Write out header to file (use the readback values where they might differ from the requested ones). */
 	outprintf ("#Data from       %s:\n", DEV_NAME);
	outprintf ("#mode:           %s\n", mode_arg);
 	outprintf ("#freq_hz:        %.3f\n", readback_hz);
	outprintf ("#interval_s:     %4.9f\n", sample_interval);
	outprintf ("#samples:        %lld\n", (long long)num_samples_per_frame);
	outprintf ("#frames:         %lld\n", (long long)num_samples_per_frame);
	outprintf ("#group_size:     %d\n", group_size);
	outprintf ("#group_interval: %d\n", group_interval);
	outprintf ("#guard_pre:  %d\n", guard_pre);
	outprintf ("#guard_post: %d\n", guard_post);
	if (mode == IMAGE || mode == IMAGE_CDS){
		outprintf ("#pixels:         %d\n", num_pixels);
		outprintf ("#guard_int:      %d\n", guard_internal);
	}
	if (mode == CDS_M){
		outprintf ("#cds_m_num:      %d\n", num_cdsm);
	}
	outprintf ("#channels:   %s\n", INPUT_CHANNELS);
 	outprintf ("#voltage:    %.3f\n", readback_v1);
	outprintf ("#gain:       %.1f\n", readback_g);
	outprintf ("#coupling:   %s\n", INPUT_COUPLING_STR);
	outprintf ("#terminal:   %s\n", TERMINAL_MODE_STR);
	outprintf ("#trigger:    %s\n", TRIGGER_EDGE_STR);
	outprintf ("#trigger_compensation:   %d\n",  TRIGGER_EARLY_BY);
	outprintf ("#trigger_compensation_s: %f\n", (TRIGGER_EARLY_BY * sample_interval) );

	/* Include the parseable data format in the output file, as well as -h above */
	if (mode == LINREG){
		outprintf ("#Data Format for lin_reg is: frame_number, end_timestamp, overload_occurred, missed_trigger, b_Dx (0,1,2,3), a (0,1,2,3),  b (0,1,2,3), s (0,1,2,3), se_a (0,1,2,3), se_b (0,1,2,3), r (0,1,2,3), min (0,1,2,3), max (0,1,2,3)\n");
	}else if (mode == CDS_M){
		outprintf ("#Data Format for cds_m is: frame_number, end_timestamp, overload_occurred, missed_trigger, D_cds (0,1,2,3),  se_b_cds (0,1,2,3), min (0,1,2,3), max(0,1,2,3)\n");
	}else if (mode == RAW){
		outprintf ("#Data Format for raw is: data_0, data_1, data_2, data_3\n");
	}else if (mode == IMAGE){
		outprintf ("#Data Format for image is: quad_0, quad_1, quad_2, quad_3\n");
	}else if (mode == IMAGE_CDS){
		outprintf ("#Data Format for image_differential is: quad_0_{frame_even - frame_odd}, quad_1_{frame_even - frame_odd},  quad_2_{frame_even - frame_odd},  quad_3_{frame_even - frame_odd}\n");
	}

	//Set handler for Ctrl-C. Within the outer-while-loop, Ctrl-C will stop cleanly at the end of the current frame, not kill the program. */
	signal(SIGINT, handle_signal_cc);

	/* Main loop, looping over -m frames of -n samples. (frames may be in -g groups (as a single task), with a single trigger pulse). */
	/* NB, grouping of frames ONLY affects whether we omit some of the taskStop...taskStart overhead [and increase samples per task]: it doesn't affect any of the per-frame data processing. */
	while (1){

		/* Are we done? Break out of the loop if we have reached max loops, or received Ctrl-C. (But don't stop just yet; we still have to process the last frame's data). */
		do_break = ( ( (frame == num_frames) && (num_frames != -1) ) || (terminate_loop) );

		/* Start the task. Start the sample-clock running on RTSI6 (because we are using "pre-triggering" aka "reference-triggering".) Sampling will start as soon as we receive the external trigger */
		if (do_break){
			gettimeofday(&task_started, NULL); /* Break. Don't start the task. Fudge the task_started time, so that the final iteration doesn't get a negative value stopstart_interval. */
		}else if (group_pos != 0){
			vdeprintf ("Not starting task, already running in group. (frame: %d, group_pos %d)...\n", frame, group_pos);
			gettimeofday(&task_started, NULL); /* In a group of frames; task already running. Fudge the task_started time. */
		}else{
			vdeprintf ("Starting task (frame %d), [Also starts to send SampleClock out on %s so PulseBlaster HW_Trigger will succeed] ...\n", frame, RTSI6);
			handleErr( DAQmxStartTask(taskHandle) );
			state = "Ready/Running";
			gettimeofday(&task_started, NULL);
			if (frame == 0){	/* Make it explicit, especially if we have just received a trigger and failed to respond to it because we were not ready! */
				eprintf ("NI4462 waiting for trigger.\n");
			}
			if (do_triggerready_delete){ /* Tell an external process that we are ready for our trigger. Only useful on first iteration */
				deprintf ("Deleting Trigger-Ready signal-file '%s'.\n", triggerready_filename);
				unlink (triggerready_filename);
				do_triggerready_delete = 1; 
			}
		}

		/* Start of frame (NB this is before the trigger pulse arrives, NOT once triggered). */
		gettimeofday(&frame_start, NULL);

		/* Here, the NI 4462 will pause, waiting for trigger. Once it gets the trigger, it will start sampling. */
		/* While this is happening, we have time to do some summary calculations for the PREVIOUS frame, and output the data.. */
		if (frame > 0 ){
			prev_frame = frame - 1;

			/* How long did we spend in the "trivial" process "stopTask...startTask"? This should be near instant, but isn't. */
			stopstart_interval = timestamp_diff (task_started, task_prestop);
			if (group_pos == 0){	/* task overhead */
				deprintf ("TaskStop()...TaskStart() overhead took %.3f ms.\n", stopstart_interval * 1000);
				if (stopstart_interval > (SLOW_TASKLOOP_DETECT_MS / 1000)){   /* warn if > 1ms. */
					if (debug){
						feprintf ("Fatal Error: TaskStop()...TaskStart() took %.3f ms in frame %d. This exceeds the warning threshold, %f ms.\n", (stopstart_interval * 1000), prev_frame, SLOW_TASKLOOP_DETECT_MS);
					}else{
						eprintf ("WARNING: TaskStop()...TaskStart() took %.3f ms in frame %d. This exceeds the warning threshold, %f ms.\n", (stopstart_interval * 1000), prev_frame, SLOW_TASKLOOP_DETECT_MS);
					}
				}
			}else{			/* no task overhead, possibly some interval samples discarded though. */
				deprintf ("Inter-frame interval (within the same group) took %.3f ms.\n", stopstart_interval * 1000);
			}

			/* Detect missed triggers. Measure the inter-GROUP_end interval from 0->1. Then check for each successive n->n+1 whether it is within 30% of this. */
			/* A missed trigger would be very problematic.  Note: this is evaluated every frame, but the timing refers to groups (which may be the same as frames, or may be bigger). */
			missed_trigger = 0;  /* In groups 0 and 1, we cannot detect missed triggers; must just assume OK. */
			if (group_pos == 0){
				if (group == 1){
					first_trigger_interval = timestamp_diff (group_end, group_end_prev);
				}else if (group > 1){
					this_trigger_interval =  timestamp_diff (frame_end, group_end_prev);
					if ( (this_trigger_interval/first_trigger_interval > MISSED_TRIGGER_DETECT) || (first_trigger_interval/this_trigger_interval > MISSED_TRIGGER_DETECT) ){
						missed_trigger = 1;
						eprintf ("WARNING: a missed trigger has occurred. Intergroup interval (between groups %d and %d) was %.3g ms; expect %.3g ms. (Threshold: %.4g).\n", group-1, group, this_trigger_interval*1e3, first_trigger_interval*1e3, MISSED_TRIGGER_DETECT);
					}
				}
			}
			group_end_prev = frame_end;

			/* Calculate the stats for previous frame, (frame -1) */
			deprintf ("Processing data for frame %d.\n", prev_frame);
			n =  samples_read_inner - (guard_pre + guard_post);	/* Already ensured >=3 above, so ok to calculate stats. */
			for (c=0; c < DEV_NUM_CH; c++){
				b    [c]  =  ( n * S_xy[c] -  S_x[c] * S_y[c] ) /  ( n * S_xx[c] - pow(S_x[c],2) );		/*  b-hat, estimator for gradient. */
				a    [c]  =  ( S_y[c] / n ) - ( b[c] * S_x[c] / n);						/*  a-hat, estimator for y-intercept. */
				s    [c]  =  sqrt(fabs( (1.0 / (n * (n-2))) * ( n * S_yy[c] - pow(S_y[c],2) - (pow(b[c],2) * (n * S_xx[c] - pow(S_x[c],2)) ) )));  /* sigma-hat, (estimator of std-dev of noise) */
				se_b [c]  =  sqrt (fabs ( ( n * pow(s[c],2) ) / ( n * S_xx[c] - pow(S_x[c],2) ) ) );		/* std err in b-hat */
				se_a [c]  =  sqrt ( pow(se_b[c],2) * S_xx[c] / n );						/* std err in a-hat */
				r    [c]  =  ( (n * S_xy[c]) - (S_x[c] * S_y[c]) ) / sqrt(fabs( (n * S_xx[c] - pow(S_x[c],2)) * (n * S_yy[c] - pow(S_y[c],2)) ));  /* r-hat, estimator for Pearson's product-moment-correlation-coefficient. */
				b_Dx [c]  =  b[c] * n;										/* b_delta_x:  best estimate for the total change in signal */
				mean [c]  =  S_y[c] / n;									/* sample mean */
				stdev[c]  =  sqrt(fabs( (1.0/(n-1)) * (S_yy[c] - (pow(S_y[c],2) / n)) ) );			/* sample std-dev */
			      /*min  [c]     already calculated */
			      /*max  [c]     already calculated */
			        D_cds[c]  =  ((S_y_g2[c] - S_y_g1[c])/num_cdsm) * (n/(n - num_cdsm));				/* Best estimate for delta, using CDS_m. */
				stdev_cds_g1[c] = sqrt(fabs( (1.0/(num_cdsm-1)) * (S_yy_g1[c] - (pow(S_y_g1[c],2) / num_cdsm) ) ));  /* stddev for 1st cds half */
				stdev_cds_g2[c] = sqrt(fabs( (1.0/(num_cdsm-1)) * (S_yy_g2[c] - (pow(S_y_g2[c],2) / num_cdsm) ) ));  /* stddev for 2nd cds half */
				se_b_cds[c]  =  quadrature_add2 ( stdev_cds_g1[c], stdev_cds_g2[c] ) / num_cdsm;		  /* overall stddev in the estimate of the gradient. (i.e. scale by 1/num_cds) */
			}

			if (mode == LINREG){  		/* Linear regression mode */

				/* Human-readable summary. NB: Error_uV is the error in the estimate of Delta_uV. */
				outprintf ("#Frame: %4d; Endtime: %.9f; Delta_uV: % f, % f, % f, % f; Error_uV: % f, % f, % f, % f; Total_uV: %f +/- %f; Ovload: %s; MissTrig: %s\n",
					prev_frame, correct_timestamp(frame_end, sample_interval),
					b_Dx[0]*1e6, b_Dx[1]*1e6, b_Dx[2]*1e6, b_Dx[3]*1e6,	  se_b[0]*n*1e6, se_b[1]*n*1e6, se_b[2]*n*1e6, se_b[3]*n*1e6,
					(b_Dx[0] + b_Dx[1] + b_Dx[2] + b_Dx[3])*1e6,
					quadrature_add4 (se_b[0], se_b[1], se_b[2], se_b[3]) *n*1e6,
					(overload_occurred?"OVL":"OK"), (missed_trigger?"MISS":"OK"));

				/* Parseable data: all one line, tab-separated. Also, see above where this is documented. Consider %g instead? */
				outprintf ("%d\t%f\t%d\t%d\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\n",
					prev_frame, correct_timestamp(frame_end, sample_interval), (int)overload_occurred, missed_trigger, b_Dx[0], b_Dx[1], b_Dx[2], b_Dx[3],
					a[0], a[1], a[2], a[3],	   b[0], b[1], b[2], b[3],   s[0], s[1], s[2], s[3],   se_a[0], se_a[1], se_a[2], se_a[3],   se_b[0], se_b[1], se_b[2], se_b[3],
					r[0], r[1], r[2], r[3],    min[0], min[1], min[2], min[3],  max[0], max[1], max[2], max[3]);

			}else if (mode == CDS_M){	/* Correlated double sampling, with multiple, averaged reads */

				/* Human-readable summary */
				outprintf ("#Frame: %4d; Endtime: %.9f; Delta_uV: % f, % f, % f, % f; Error_uV: % f, % f, % f, % f; Total_uV: %f +/- %f; Ovload: %s; MissTrig: %s\n",
				prev_frame, correct_timestamp(frame_end, sample_interval), D_cds[0]*1e6, D_cds[1]*1e6, D_cds[2]*1e6, D_cds[3]*1e6,
				se_b_cds[0]*n*1e6, se_b_cds[1]*n*1e6, se_b_cds[2]*n*1e6, se_b_cds[3]*n*1e6,
				(D_cds[0] + D_cds[1] + D_cds[2] + D_cds[3])*1e6,
				quadrature_add4 (se_b_cds[0], se_b_cds[1], se_b_cds[2], se_b_cds[3]) *n*1e6,
				(overload_occurred?"OVL":"OK"), (missed_trigger?"MISS":"OK"));

				/* Parseable data. */
				outprintf ("%d\t%f\t%d\t%d\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\t%.9f\n",
				prev_frame, correct_timestamp(frame_end, sample_interval), (int)overload_occurred, missed_trigger,
				D_cds[0], D_cds[1], D_cds[2], D_cds[3],  se_b_cds[0], se_b_cds[1], se_b_cds[2], se_b_cds[3],
				min[0], min[1], min[2], min[3],  max[0], max[1], max[2], max[3]);

			}else if (mode == RAW){		/* Raw data mode. */

				/* Human-readable summary: mean/stdev rather than linreg. */
				outprintf ("#Frame: %4d; Endtime: %.9f; Means_uV: % f, % f, % f, % f; StdDev_uV: % f, % f, % f, % f; Overall_uV: %f +/- %f; Ovload: %s; MissTrig: %s\n",
					prev_frame, correct_timestamp(frame_end, sample_interval),
					mean[0]*1e6, mean[1]*1e6, mean[2]*1e6, mean[3]*1e6, stdev[0]*1e6, stdev[1]*1e6, stdev[2]*1e6, stdev[3]*1e6,
					(mean[0]+mean[1]+mean[2]+mean[3])*1e6, quadrature_add4 ( stdev[0], stdev[1], stdev[2], stdev[3])*1e6,
					(overload_occurred?"OVL":"OK"), (missed_trigger?"MISS":"OK"));

				/* Parseable data: output the raw data (excluding the start/end guard samples), in the regular 4-column format for eg fftplot */
				for (i=0 ; (unsigned)i< (num_samples_per_frame - guard_pre - guard_post); i++){
					outprintf ("%.9f\t%.9f\t%.9f\t%.9f\n",raw[0][i], raw[1][i], raw[2][i], raw[3][i] );  /* NB in dummy-mode, with -O3 (but not -O2), gcc complains WRONGLY that "raw[X] may be used uninitialized in this function" */
				}

			}else if (mode == IMAGE){	/* Image mode */

				/* Human-readable summary. FIXME: is this really the most useful info in this case? */
				outprintf ("#Frame: %4d; Endtime: %.9f; Means_uV: % f, % f, % f, % f; StdDev_uV: % f, % f, % f, % f; Overall_uV: %f +/- %f; Ovload: %s; MissTrig: %s\n",
					prev_frame, correct_timestamp(frame_end, sample_interval),
					mean[0]*1e6, mean[1]*1e6, mean[2]*1e6, mean[3]*1e6, stdev[0]*1e6, stdev[1]*1e6, stdev[2]*1e6, stdev[3]*1e6,
					(mean[0]+mean[1]+mean[2]+mean[3])*1e6, quadrature_add4 ( stdev[0], stdev[1], stdev[2], stdev[3])*1e6,
					(overload_occurred?"OVL":"OK"), (missed_trigger?"MISS":"OK"));

				/* Parseable data */
				for (i=0 ; i<num_pixels; i++){
					outprintf ("%.9f\t%.9f\t%.9f\t%.9f\n", pixels1[0][i], pixels1[1][i], pixels1[2][i], pixels1[3][i] );
				}

			}else if (mode == IMAGE_CDS && (prev_frame%2) == 0){	/* Image Differential mode: every 2nd frame. */

				/* Human-readable summary. FIXME: is this really the most useful info in this case? NB the means and stdDevs are for the 2nd frame, not the differences! */
				outprintf ("#Frame: %4d; Endtime: %.9f; Means_uV: % f, % f, % f, % f; StdDev_uV: % f, % f, % f, % f; Overall_uV: %f +/- %f; Ovload: %s; MissTrig: %s\n",
					prev_frame, correct_timestamp(frame_end, sample_interval),
					mean[0]*1e6, mean[1]*1e6, mean[2]*1e6, mean[3]*1e6, stdev[0]*1e6, stdev[1]*1e6, stdev[2]*1e6, stdev[3]*1e6,
					(mean[0]+mean[1]+mean[2]+mean[3])*1e6, quadrature_add4 ( stdev[0], stdev[1], stdev[2], stdev[3])*1e6,
					(overload_occurred?"OVL":"OK"), (missed_trigger?"MISS":"OK"));

				/* Parseable data 4 quadrants, frame_n - frame_n-1,  where n is even. */
				for (i=0 ; i<num_pixels; i++){
					outprintf ("%.9f\t%.9f\t%.9f\t%.9f\n", (pixels2[0][i] - pixels1[0][i]), (pixels2[1][i] - pixels1[1][i]), (pixels2[2][i] - pixels1[2][i]), (pixels2[3][i] - pixels1[3][i]) );
				}
			}
		}

		/* Break now? */
		if (do_break){
			vdeprintf ("Breaking out of main loop. Frame: %d, num_frames: %d, terminate_loop: %d\n", frame, num_frames, terminate_loop);
			break;
		}

		/* --------------------------------------------------------------------------------------------- */
		/* Inner loop. Repeatedly check for data from the NI 4462, read what we can, and pre-process it. */
		/* As in ni4462_test.c, first do a non-blocking read, then (iff we got zero samples), do the blocking read. */

		/* Zero the sums for start of frame. */
		samples_read_inner = 0;
		px = 0; guard = 0;
		for (c=0; c < DEV_NUM_CH; c++){
			S_x[c] = S_xx[c] = S_y[c] = S_yy[c] = S_xy[c] = S_y_g1[c] = S_yy_g1[c] = S_y_g2[c] = S_yy_g2[c] = 0;
			min[c] = 1e10; max[c] = -1e10;
		}

		/* Interleaved reading and calculating */
		while (1){

			/* First, let the device actually do some sampling! */

			/* Non-blocking read of all the samples that are available. May return 0 or more samples (automatically safely limited by size of buffer). DAQmx_Val_Auto is non-blocking when DAQmxSetReadReadAllAvailSamp(TRUE) above */
			/* Can only do the non-blocking read with group_size == 1: it could otherwise acquire MORE samples than we want in this frame! Non-blocking is desirable; it allows us to pre-process while still acquiring. */
			if (group_size == 1){
				vdeprintf  ("Non-blocking read of as many samples as available...\n");
				handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &samples_read_thistime, NULL) );
			}else{
				vdeprintf  ("Blocking read of exactly one frame of samples...\n");  /* Have to do it this way; else we could read part of the next frame. */
				handleErr( DAQmxReadAnalogF64(taskHandle, (num_samples_per_frame - samples_read_inner), DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &samples_read_thistime, NULL) );
			}

			n_this = samples_read_inner;
			samples_read_inner += samples_read_thistime;
			samples_read_total += samples_read_thistime;
			vdeprintf  ("   ...acquired %d points this time; loop_total is: %lld.\n",(int)samples_read_thistime, (long long)samples_read_inner);

			/* IFF we got zero samples, now do a blocking read, and wait. Otherwise, optimise: guess that it won't block, so proceed to the next iteration. */
			if (samples_read_thistime == 0){
				/* Blocking read of 1 sample. */
				/* This is where we block, if waiting for an external trigger input */
				if ( samples_read_total == 0) {   /* First ever trigger */
					eprintf ("Waiting for first external trigger (%s edge)...\n", TRIGGER_EDGE_STR);
				}else if ( (samples_read_inner == 0)  ){
					vdeprintf ("Waiting for external trigger for frame %d (blocking read)...\n", frame);
				}
				handleErr( DAQmxReadAnalogF64(taskHandle, 1, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &samples_read_thistime, NULL) );
				samples_read_inner += samples_read_thistime;
				samples_read_total += samples_read_thistime;
				vdeprintf  ("...acquired %d points this time; loop_total is: %lld.\n",(int)samples_read_thistime, (long long)samples_read_inner);
			}

			/* Dump (prefixed) raw data, if desired. Format for parseability:  #=frame_num,sample_num:\tval0\tval1\tval2\tval3. Don't skip the guard samples here. */
			if (dump_raw){
				for (i=0; i < samples_read_thistime; i++){
					outprintf ("#=%d,%d:\t% .9f\t% .9f\t% .9f\t% .9f\n", frame, (unsigned int)(n_this+i), data[DEV_NUM_CH*i+0], data[DEV_NUM_CH*i+1], data[DEV_NUM_CH*i+2],  data[DEV_NUM_CH*i+3] );
				}
			}

			/* Pre-process the data for this subgroup of this frame. */
			/* Note: The counter i advances with the captured samples; px advances with the non-guard samples. */
			for (i=0; i < samples_read_thistime; i++){
				if ( (unsigned int)(n_this + i) < (unsigned int)(guard_pre) ){  				/* Skip first guard sample(s) */
					continue;
				}
				if ( (unsigned int)(n_this + i) >= (unsigned int)(num_samples_per_frame - guard_post) ){	/* Skip final guard sample(s) */
					continue;
				}
				if ( (mode == IMAGE) || (mode == IMAGE_CDS) ){							/* Skip internal guard sample(s) in IMAGE modes */
					if ( guard % (guard_internal+1) != 0) {
						guard++;
						continue;
					}
				}

 				for (c=0; c < DEV_NUM_CH; c++){		/* Statistics.*/
 					S_x  [c] +=  px;
 					S_xx [c] +=  (px*px);
 					S_y  [c] +=  data [DEV_NUM_CH*i +c];
 					S_yy [c] +=  data [DEV_NUM_CH*i +c] * data [DEV_NUM_CH*i +c];
 					S_xy [c] +=  px * data [DEV_NUM_CH*i +c];
 					min  [c] =   (data [DEV_NUM_CH*i +c] < min[c]) ? data [DEV_NUM_CH*i +c] : min [c] ;
 					max  [c] =   (data [DEV_NUM_CH*i +c] > max[c]) ? data [DEV_NUM_CH*i +c] : max [c] ;
				}

				if ( px < num_cdsm) {					/* CDS sum for group 1 */
					for (c=0; c < DEV_NUM_CH; c++){
						S_y_g1 [c]  += data [DEV_NUM_CH*i +c];
						S_yy_g1 [c] += data [DEV_NUM_CH*i +c] * data [DEV_NUM_CH*i +c];
					}
				}else if ( (unsigned int)px >= (num_samples_per_frame - guard_post - num_cdsm) ){
					for (c=0; c < DEV_NUM_CH; c++){			/* CDS sum for group 2 */
						S_y_g2  [c] += data [DEV_NUM_CH*i +c];
						S_yy_g2 [c] += data [DEV_NUM_CH*i +c] * data [DEV_NUM_CH*i +c];
					}
				}

				/* If mode is RAW, save it for later (after outputting the summary header) */
				if (mode == RAW){
					for (c=0; c < DEV_NUM_CH; c++){
						raw [c][px] = data [DEV_NUM_CH*i +c];
					}
				}

				/* If mode is IMAGE or IMAGE_CDS then save the data into the relevant pixels_x array */
				if ( (mode == IMAGE) || (mode == IMAGE_CDS) ){
					pixels = ((mode == IMAGE_CDS) && (frame%2)) ? pixels2 : pixels1 ; /* Destination? In Image_CDS mode, odd and even frames go into different arrays */
					for (c=0; c < DEV_NUM_CH; c++){
						pixels [c][px] = data [DEV_NUM_CH*i +c];
					}
				}

				px++;
				guard++;
			}

			/* Have we now got all the samples we need for this loop? */
			if ( samples_read_inner == num_samples_per_frame) {
				vdeprintf ("Finished acquiring all %lld samples for this frame (%d)... breaking out of inner loop.\n", (long long)num_samples_per_frame, frame);
				break;
			}
		}

		/* End of sampling */
		gettimeofday(&frame_end, NULL);

		/* --------------------------------------------------------------------------------------------- */

		/* (!!) Here, the NI4462 is done. We might have very little time between the last sample being read (i.e. end of this frame), and the start of the next frame. */

		/* Check for overload, during the previous frame. Must do this before stopping the task. Non-fatal, except in debug mode. */
		handleErr( DAQmxGetReadOverloadedChansExist (taskHandle, &overload_occurred) );   /* NB Performing this check also clears the overload 'flag' */
		if (overload_occurred){	/* Get details of which channel it was. */
			handleErr( DAQmxGetReadOverloadedChans(taskHandle, error_buf, sizeof(error_buf) ) );  /* NB must only do this after DAQmxGetReadOverloadedChansExist() said yes. */
			if (debug){
				feprintf ("Fatal Error: an overload has occurred, frame %d, in channel(s): '%s'.\n", frame, error_buf);
			}else{
				eprintf ("WARNING: an overload has occurred, frame %d, in channel(s): '%s'.\n", frame, error_buf);
			}
		}

 		/* Stop the task. Then loop back, and start again immediately. Do NOT clear the task; we can go back and start again, using the same configuration. */
		/* If we are WITHIN a group of frames, don't actually stop the task. Just save the timestamp, and if necessary, discard some intervening samples. */
		group_pos++;
		if (group_pos == group_size){		/* End of group (or the group-size is 1). Really stop the task; will then be started anew. */
			gettimeofday(&group_end, NULL);
			group_pos = 0;
			group++;

			gettimeofday(&task_prestop, NULL);
			vdeprintf ("Stopping task (frame %d).\n", frame);
			handleErr( DAQmxStopTask(taskHandle) );
			state = "Stopped";
		}else{					/* Within a group; don't stop the task (but fake the timestamp) */
			gettimeofday(&task_prestop, NULL);
			vdeprintf ("Continuing task within a group. (frame: %d, group_pos: %d)...\n", frame, group_pos);
			if (group_interval > 0){	/* If necessary, skip samples for the group-interval. (We can't have another trigger-pulse, so use dead-reckoning). */
				/* Blocking read of group_interval samples; throw these away; they are simply padding. */
				n_discard = group_interval;
				vdeprintf  ("Discarding %lld points for interval between frames in the same group.\n", (long long)n_discard);
				while (n_discard > 0){	/* [discard in chunks of n_discard; group_interval could exceed BUFFER_SIZE_TUPLES]. */
					n = (n_discard > BUFFER_SIZE_TUPLES) ? BUFFER_SIZE_TUPLES : n_discard;   
					handleErr( DAQmxReadAnalogF64(taskHandle, n, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &samples_read_thistime, NULL) );
					n_discard -= samples_read_thistime;
				}
			}
		}

		frame++ ;
	}

	/* Clear task: we're done. This discards its configuration. [even if we omit this call, it is implicit when this program exits. */
	handleErr( DAQmxClearTask(taskHandle) );

	/* Free memory for the pixel arrys (not strictly necessary at program end.) */
	if ( (mode == IMAGE) || (mode || IMAGE_CDS) ){
		for (i=0; i < DEV_NUM_CH; i++){
			free (pixels1[i]);
			pixels1[i] = NULL;
			if (mode == IMAGE_CDS){
				free (pixels2[i]);
				pixels2[i] = NULL;
			}
		}
	}else if (mode == RAW){
		for (i=0; i < DEV_NUM_CH; i++){
			free (raw[i]);
			raw[i] = NULL;
		}
	}

	/* Done! */
	deprintf ("Cleaning up after libnidaqmx: removing lockfiles from NI tempdir, %s .\n", LIBDAQMX_TMPDIR)   /* libdaqmx should clean up its own lockfiles, but doesn't. */
	ret = system ( "rm -f "LIBDAQMX_TMPDIR"ni_dsc_osdep_*" );   /* Risky. Part of the path is hardcoded here, as a slight safety measure. */
 	if (ret != 0){
		deprintf ("Problem cleaning up.\n")
	}
	fclose ( outfile );
	return 0;
}
