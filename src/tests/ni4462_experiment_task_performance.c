/* Copyright (C) Richard Neill 2011-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software, You can
 * redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.  An exception is granted to link against 
 * the National Instruments proprietary libraries, such as libnidaqmx. There is NO WARRANTY, neither express nor implied.
 * For the details, please see: http://www.gnu.org/licenses/gpl.html */

/* simple performance check for the looped task */

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
#define DEV_NAME			"NI 4462"			/* Product name */
#define DEV_DEV				"Dev1"				/* Which PCI device to use. */
#define DEV_TRIGGER_INPUT		"PFI0"				/* Name of the Digital trigger input */
#define DEV_NUM_CH			4				/* Number of input channels on this one. Avoids (some) hardcoded "4"s. */
#define DEV_FREQ_MIN			32.0				/*    Min freq (Hz) */
#define DEV_FREQ_MAX			204800.0			/*    Max freq (Hz) */
#define DEV_FREQ_QUANTISATION		"181.9 uS/s"			/* Frequency quantisation, to nearest micro sample-per-second */
#define DEV_SAMPLES_MIN			2				/* Min number of samples at a time. (experimentally) */
#define DEV_SAMPLES_MAX			16777215			/* 2^24 - 1, measured experimentally */
#define DEV_ADC_FILTER_DELAY_SAMPLES	63				/* ADC filter standard delay. (But depends on sample freq if Low-Frequency Alias Rejection Enabled) */

/* CONFIGURATION CHOICES. */						/* (Experiment with ni4462_test.c, if desired, then set here. */
#define INPUT_CHANNELS			"ai0:3"				/* All channels. Options: ai0, ai1, ai2, ai3, ai0:3  */

#define VOLTAGE_MAX			0.316				/* Options: 0.316, 1, 3.16, 10, 31.6, 100.  x means [-x,+x]. */

#define INPUT_COUPLING			DAQmx_Val_DC			/* Options: DAQmx_Val_DC, DAQmx_Val_AC */
#define INPUT_COUPLING_STR		"dc"				/* NB: if AC, remember the settling time AFTER taskCommit: see manual, section: Analog Input Channel Configurations -> Input Coupling  */

#define TERMINAL_MODE			DAQmx_Val_Diff			/* Options: DAQmx_Val_Diff, DAQmx_Val_PseudoDiff */
#define TERMINAL_MODE_STR		"diff"

#define TRIGGER_EDGE			DAQmx_Val_Falling		/* Options: DAQmx_Val_Rising, DAQmx_Val_Falling */
#define TRIGGER_EDGE_STR		"falling"

#define ENABLE_ADC_LF_EAR		1				/* Bool: Enable low frequency enhanced alias rejection (though probably irrelevant if we are sampling fast) */
#define INT_CLOCK_EDGE  		DAQmx_Val_Rising		/* Sample on the RE of the Internal clock. (Probably doesn't matter). */
#define INT_CLOCK_EDGE_STR  		"rising"

/* Default values */
#define DEFAULT_SAMPLE_HZ		200000				/* Default to 200 kHz. (Can go slightly faster) */
#define DEFAULT_COUNT			10000				/* Default to 10,000 samples (per frame) */
#define DEFAULT_MAXFRAMES		0				/* Max number of frames before exit. 0 means loop forever. */

/* Buffer sizes */
#define BUFFER_SIZE			(25000 * DEV_NUM_CH)		/* max_number_of_data_points_read_at_a_time * number_of_channels. Experimentally, < 400*4 limits performance, though 10*4 is safe. RAM is plentiful, so say 25k * 4. */


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
TaskHandle taskHandle = 0;


/* Show help */
void print_help(char *argv0){
	fprintf (stderr,"This is a performance check for the time taken to do various task-related things in a loop.\n"
			"Try clocking the external PFI0 input at 1 MHz (so that triggers arrive immediately)\n"
			"Then, use 'sudo nice -n -20 %s -f 200000 -n 200 -m 50'.\n"
			"Alternatively, send it a pulse train of 50 pulses (eg 'ni4462_pb_trigger.sh 1ms,50x' ) and\n"
			"see how many of them are caught vs how many are missed.\n"
			"(To ensure the CPU stays at max frequency, run use 'nice yes >/dev/null' on another core'\n\n"
			"USAGE:  %s  [OPTIONS]  \n"
			"OPTIONS:\n"
			"        -f   FREQ         sample frequency (in Hz)\n"
			"        -n   NUM          number of samples per frame\n"
			"        -m   MAXFRAMES    stop after this many frames (otherwise, never stop till Ctrl-C)\n"
			"        -d                enable debug (very verbose, messes up timing measurments)\n"
			"        -h                show this help.\n\n"
			,argv0, argv0);
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


/* Signal handler: handle Ctrl-C in middle of main loop. */
void handle_signal(int signum){
	eprintf ("Ctrl-C (sig %d), stopping at the end of this (complete) frame. (Use Ctrl-\\ to kill now).\n", signum);
	terminate_loop = 1; 	/* Break out of the while loop this time round,. */
}


/* Do it... */
int main(int argc, char* argv[]){

	int	opt; extern char *optarg; extern int optind, opterr, optopt;       /* getopt */
	char*   input_channels = DEV_DEV"/"INPUT_CHANNELS;
	uInt64	num_samples_per_frame = DEFAULT_COUNT;
	float64 sample_rate = DEFAULT_SAMPLE_HZ;
	int     num_frames = DEFAULT_MAXFRAMES;
	int32   he_retval = 0;   		/* Used by #define handleErr() above */
	float64 readback_hz = 0, readback_v1 = 0, readback_v2 = 0, readback_g;
	int32   samples_read_thistime;		/* Number of samples (per channel) that were actually read in this pass */
	uInt64  samples_read_inner = 0;		/* Number of samples (per channel) that have been read in the inner loop */
	uInt64  samples_read_total = 0;		/* Number of samples (per channel) that have been read so far in (grand) total */
	int     ret, do_break, frame = 0;
	float64 data[BUFFER_SIZE];		/* Our read data buffer. Multiple of 4. Needn't have room for num_samples_per_frame all at once */
	FILE *  outfile = stdout;
	struct  timeval	then, now;
	double  deltat1 =0 ,  deltat2 =0, deltat3 =0 , deltat4=0;

	/* Parse options and check for validity */
        if ((argc > 1) && (!strcmp (argv[1], "--help"))) {      /* Support --help, without the full getopt_long */
                print_help(argv[0]);
                exit (EXIT_SUCCESS);
        }

        while ((opt = getopt(argc, argv, "dhf:n:m:")) != -1) {  /* Getopt */
                switch (opt) {
                        case 'h':                               /* Help */
				print_help(argv[0]);
				exit (EXIT_SUCCESS);
				break;
			case 'd':				/* Debugging */
				debug = 1;
				break;
			case 'f':				/* Sample Frequency. Float64 */
				sample_rate = atof(optarg);
				if ( (sample_rate < DEV_FREQ_MIN) || (sample_rate > DEV_FREQ_MAX) ){  /* Limits of device */
					feprintf ("Fatal Error: sample rate must be between %f and %f Hz.\n", DEV_FREQ_MIN, DEV_FREQ_MAX);
				}
				break;
			case 'n':				/* Number of samples to capture. Integer.  */
				num_samples_per_frame = strtoll (optarg, NULL, 0);
				if (num_samples_per_frame <= 0){
					feprintf ("Fatal Error: number of samples per frame must be > 0.\n");
				}else if (num_samples_per_frame < DEV_SAMPLES_MIN){
					feprintf ("Fatal Error: number of samples per frame must be >= %d.\n", DEV_SAMPLES_MIN);
				}else if (num_samples_per_frame > (DEV_SAMPLES_MAX) ){  /* Too many samples for finite mode. */
					feprintf ("Fatal Error: number of samples per frame must be <= %d. [Recompile with continuous reading instead, see ni4462_test.c].\n", DEV_SAMPLES_MAX);
				}
				break;
			case 'm':				/* Max frames. "cont" for continuous */
				if (!strcasecmp(optarg, "cont")){
					num_frames = 0;
				}else{
					num_frames = atoi (optarg);
					if (num_frames <= 0){
						feprintf ("Fatal Error: max frames must be > 0, (or 'cont' for continuous).\n");
					}
				}
				break;
			default:
				feprintf ("Unrecognised argument %c. Use -h for help.\n", opt);
				break;
		}
	}

	if (argc - optind != 0){
		feprintf ("This takes exactly zero non-optional arguments. Use -h for help.\n");
	}
	if (num_samples_per_frame < 3){
		feprintf ("Number of samples per frame must be 3 or more. Otherwise, the statistics can't be calculated.\n");
	}

	/* Now create and configure the DAQmx task. See ni4462_test.c for a verbosely commented and debugging example. */

	/* Create Task (name is arbitrary, but avoid "" which sometimes leaks memory) */
	handleErr( DAQmxCreateTask("Arbitrary_name",&taskHandle) );

	/* Set input channels: choose the channel(s), the terminal-mode, and the voltage-scale. NB the voltage scale is coerced by device capabilities. */
	handleErr( DAQmxCreateAIVoltageChan(taskHandle, input_channels, "VoltageInput", TERMINAL_MODE, -VOLTAGE_MAX, VOLTAGE_MAX, DAQmx_Val_Volts, NULL) );  /* DAQmx_Val_Volts is the scale-type */
 	handleErr( DAQmxGetAIMin (taskHandle, input_channels, &readback_v1)  ); 	/* Check coercion */
 	handleErr( DAQmxGetAIMax (taskHandle, input_channels, &readback_v2) );
	handleErr( DAQmxGetAIGain(taskHandle, input_channels, &readback_g) );
	deprintf ("Input Voltage range requested: [%f, %f] V; actually coerced by device to: [%f, %f] V. Gain is: %f dB. Terminal_mode: %s.\n", -VOLTAGE_MAX, VOLTAGE_MAX, readback_v1, readback_v2, readback_g, TERMINAL_MODE_STR);

	/* Configure Channel input-coupling (AC/DC). NB if choosing AC coupling, remember to allow the settling time! */
	deprintf  ("Setting input_coupling to %d, %s ...\n", INPUT_COUPLING, INPUT_COUPLING_STR );
	handleErr( DAQmxSetAICoupling (taskHandle, input_channels, INPUT_COUPLING) );

	/* Configure Triggering. Set trigger input to digital triggering via the external SMA connector, and select the edge. (Omit this, to use automatic trigger at StartTask) */
	deprintf  ("Setting triggering to external trigger input, %s, using %s edge...\n", DEV_TRIGGER_INPUT, TRIGGER_EDGE_STR );
	handleErr ( DAQmxCfgDigEdgeStartTrig (taskHandle, DEV_TRIGGER_INPUT, TRIGGER_EDGE) );

	/* Configure Timing and Sample Count. [Use Rising Edge of Onboard clock (arbitrary choice).] */
	/* Acquire finite number, num_samples_per_frame, of samples (on each channel) at a rate of (coereced)sample_rate. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, OnboardClock, sample_rate, INT_CLOCK_EDGE, DAQmx_Val_FiniteSamps, num_samples_per_frame ) );  /* Finite number of samples */
	handleErr( DAQmxGetSampClkRate (taskHandle, &readback_hz)  ); 	/* Check coercion */
	deprintf ("Acquiring (finite) %lld samples per task. Sample clock requested: %f Hz; actually coerced to: %f Hz. Using %s edge of the internal sample-clock.\n", num_samples_per_frame, sample_rate, readback_hz, INT_CLOCK_EDGE_STR);

	/* Ensure that DAQmxReadAnalogF64() (below) will not block for completion, but will read all the available samples. */
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, TRUE) );

	/* Set EnhancedAliasRejectionEnable as #defined above. Usually on, but irrelevant at higher frequencies anyway. */
	handleErr( DAQmxSetAIEnhancedAliasRejectionEnable(taskHandle, input_channels, ENABLE_ADC_LF_EAR) );

	/* Commit the task (make sure all hardware is configured and ready). */
	deprintf ("Committing task (%d)\n", DAQmx_Val_Task_Commit);
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) );  /* Error: DAQmxErrorPALResourceReserved  i.e. -50,103  arises if there are two processes contending for access to this device. */

	/* Ready to go... print a brief summary */
	eprintf ("Configuration: Freq: %f  Hz, Sampless per frame: %ld,  Max Frames: %d .\n", sample_rate, (unsigned long)num_samples_per_frame, num_frames);

	//Set handler for Ctrl-C. Within the outer-while-loop, Ctrl-C will stop cleanly at the end of the current frame, not kill the program. */
	signal(SIGINT, handle_signal);

	/* THIS WOULD REALLY REALLY HELP if it were supported for this device ! */
	//handleErr (DAQmxSetStartTrigRetriggerable( taskHandle, TRUE) );

	/* Main loop, looping over each frame of N samples */
	while (1){

		/* Are we done? Break out of the loop if we have reached max loops, or received Ctrl-C. (But don't break yet; we still have to process the last frame's data].  */
		do_break = ( ( (frame == num_frames) && (num_frames != 0) ) || (terminate_loop) );

		/* Start the task. Sampling will start as soon as we receive the external trigger. */
		if (!do_break){

			/* Start task (and time it) */
			gettimeofday(&then, NULL);
			vdeprintf ("Starting task (frame %d)...\n", frame);
			handleErr( DAQmxStartTask(taskHandle) );
			gettimeofday(&now, NULL);
			deltat1 = (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec));

			/* Printf data (and time the printf) */
			gettimeofday(&then, NULL);
			eprintf("Frame: %3d.  TaskStart: %6.3g ms,   ReadLoop: %6.3g ms,  TaskStop: %6.3g ms,   Printf %6.3g ms\n", frame, deltat1*1000, deltat2*1000, deltat3*1000, deltat4*1000);
			gettimeofday(&now, NULL);
			deltat4 = (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec));

		}

		/* Here, the NI 4462 will pause, waiting for trigger. Once it gets the trigger, it will start sampling. */
		/* While this is happening, we have time to do some summary calculations for the PREVIOUS frame. */
		if (frame > 0 ){
			/* Would do something here, but removed, for max speed of timing loop */
		}

		/* Break now? */
		if (do_break){
			vdeprintf ("Breaking out of main loop. frame: %d, num_frames: %d, terminate_loop: %d\n", frame, num_frames, terminate_loop);
			break;
		}

		/* --------------------------------------------------------------------------------------------- */
		/* Inner loop. Repeatedly check for data from the NI 4462, read what we can, and pre-process it. */
		/* As in ni4462_test.c, first do a non-blocking read, then (iff we got zero samples), do the blocking read. */

		/* Initialise*/
		samples_read_inner = 0;

		/* Time the inner (read) loop */
		gettimeofday(&then, NULL);
		/* Interleaved reading and calculating */
		while (1){

			/* First, let the device actually do some sampling! */

			/* Non-blocking read of all the samples that are available. May return 0 or more samples (automatically safely limited by size of buffer). This is non-blocking because of DAQmxSetReadReadAllAvailSamp(TRUE) above */
			vdeprintf  ("Non-blocking read of as many samples as available...\n");
			handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, 0, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*DEV_NUM_CH)), &samples_read_thistime, NULL) );
			samples_read_inner += samples_read_thistime;
			samples_read_total += samples_read_thistime;
			vdeprintf  ("   ...acquired %d points this time; loop_total is: %lld.\n",(int)samples_read_thistime, samples_read_inner);

			/* Pre-process the data for this subgroup of this frame. (omitted) */

			/* Have we now got all the samples we need for this loop? */
			if ( samples_read_inner == num_samples_per_frame) {
				vdeprintf ("Finished acquiring all %lld samples for this frame...breaking out of inner loop.\n", num_samples_per_frame);
				break;
			}
		}
		gettimeofday(&now, NULL);
		deltat2 = (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec));

		/* --------------------------------------------------------------------------------------------- */

		/* (!!) Here, the NI4462 is done. We might have very little time between the last sample being read (i.e. end of this frame), and the start of the next frame. */

 		/* Stop the task. Then loop back, and start again immediately. Do NOT clear the task; we can go back and start again, using the same configuration. */
		gettimeofday(&then, NULL);
		vdeprintf ("Stopping task (frame %d).\n", frame);
		handleErr( DAQmxStopTask(taskHandle) );
		gettimeofday(&now, NULL);
		deltat3 = (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec));

		frame++ ;
	}

	/* Clear task: we're done. This discards its configuration. [even if we omit this call, it is implicit when this program exits. */
	handleErr( DAQmxClearTask(taskHandle) );

	/* Done! */
	deprintf ("Cleaning up after libnidaqmx: removing lockfiles from NI tempdir, %s .\n", LIBDAQMX_TMPDIR)   /* libdaqmx should clean up its own lockfiles, but doesn't. */
	ret = system ( "rm -f "LIBDAQMX_TMPDIR"ni_dsc_osdep_*" );   /* Risky. Part of the path is hardcoded here, as a slight safety measure. */
        fclose ( outfile );
	return 0;
}
