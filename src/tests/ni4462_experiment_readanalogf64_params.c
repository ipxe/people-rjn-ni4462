/* Copyright 2011-2012 Richard Neill <ni4462 at richardneill dot org>. This is Free Software, Licensed under the GNU GPL v3+, with an exception for linking against libdaqmx. */

/* What EXACTLY is the behaviour of DAQmxReadAnalogF64 wrte blocking and non-blocking reads?  Change these to find out. */
/* Trigger at 1 Hz, so that we have a good chance that we will get in a non-blocking read. before the trigger */

#define  SAMPLE_RATE_HZ		100		/* Sample rate, Hz. 32-100 makes it crash, 10000 makes it stable */
#define  MODE_INFINITE		0		/* 0 for FINITE, 1 for INFINITE (continuous) sampling */
#define  READ_AVAIL_SAMPS	TRUE		/* ReadAllAvailableSamples: TRUE or FALSE */
#define  SAMPS_TO_READ_AUTO	1		/* How many samples to try to read?  0 for N, 1 for AUTO */
#define  TIMEOUT_INFINITE	0		/* Timeout: 0 for 0, 1 for INFINITE */

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
#include <signal.h>
#include <NIDAQmx.h>	/* NI's library. Also '-lnidaqmx' */

/* Macros */
#define handleErr(functionCall) if(( he_retval=(functionCall) )){ handleErr2(he_retval); }	/* For each DAQmx call, if retval != 0, handle the error appropriately */

#define deprintf(...)	if (debug) { fprintf(stderr, __VA_ARGS__); }	/* Debug error printf: print to stderr iff debug is set */


/* Globals */
int debug = 1;
TaskHandle taskHandle = 0;

uInt64	NUM_SAMPLES_PER_FRAME = 100;
float64 SAMPLE_RATE = SAMPLE_RATE_HZ;
int32   he_retval = 0, retval; 		/* Used by #define handleErr() above */
int32   samples_read;			/* Number of samples (per channel) read in this particular call. */
int32   samples_read_total;
int     frame = 0, tries=0;
float64 data[(25000 * 4)];		/* Our read data buffer. Multiple of 4. Needn't have room for num_samples_per_frame all at once */
bool32  isdone, isdone2;
char    error_buf[2048]="\0", error_buf2[2048]="\0";


void print_help(char *argv0){		/* Help */
	fprintf (stderr, "Experimental test for the behaviour of DAQmxReadAnalogF64()\n"
		"To alter the behaviour, change #defines and recompile.\n"
		"NB, run this more than once: it doesn't always work for long, but can sometimes misleadingly survive a long time.\n"
		"Usage: %s    (no args)\n",
		argv0);
}

void handleErr2 (int error){		/* Error handling: Quit on fatal errors. */

	DAQmxGetErrorString(error, error_buf, sizeof(error_buf));  /* Get long error msg */
	DAQmxGetExtendedErrorInfo (error_buf2, sizeof(error_buf)); /* Get longer error msg */
	deprintf ("-----\nDAQmx Error (%d): %s\n%s\n----\n", error, error_buf, error_buf2);

	exit(1);
}


int main(int argc, char* argv[]){

	 /* Show help */
        if (argc > 1) {
                print_help(argv[0]);
                exit (0);
        }

	/* Create Task (name is arbitrary, but avoid "" which sometimes leaks memory) */
	handleErr( DAQmxCreateTask("Arbitrary_name",&taskHandle) );

	/* Set input channels: choose the channel(s), the terminal-mode, and the voltage-scale. NB the voltage scale is coerced by device capabilities. */
	deprintf ("Creating AI Voltage channel. Voltage range: [%f, %f] V, Terminal_mode: diff.\n", -0.316, 0.316);
	handleErr( DAQmxCreateAIVoltageChan(taskHandle, "Dev1/ai0:3", "Random_name", DAQmx_Val_Diff, -0.316, 0.316, DAQmx_Val_Volts, NULL) );  /* DAQmx_Val_Volts is the scale-type */

	/* Configure Triggering. Set trigger input to digital triggering via the external SMA connector, and select the edge. (Omit this, to use automatic trigger at StartTask) */
	deprintf  ("Setting triggering to external trigger input, %s, using falling edge...\n", "PFI0" );
	handleErr ( DAQmxCfgDigEdgeStartTrig (taskHandle, "PFI0", DAQmx_Val_Falling) );

	/* Configure Timing and Sample Count. [Use Rising Edge of Onboard clock (arbitrary choice).] */
#if MODE_INFINITE == 0
	/* Acquire finite number, num_samples_per_frame, of samples (on each channel) at a rate of (coereced)SAMPLE_RATE. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, 0, SAMPLE_RATE, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, (NUM_SAMPLES_PER_FRAME) ) );  /* Finite number of samples */
	deprintf ("Acquiring (finite) %lld samples per task. Sample clock requested: %f Hz. Using rising edge of the internal sample-clock.\n", NUM_SAMPLES_PER_FRAME, SAMPLE_RATE);
#else
	/* Acquire continuously at a rate of (coereced)SAMPLE_RATE. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, 0, SAMPLE_RATE, DAQmx_Val_Rising, DAQmx_Val_ContSamps, (NUM_SAMPLES_PER_FRAME) ) );  /* Finite number of samples */
	deprintf ("Acquiring (continously) %lld samples per task. Sample clock requested: %f Hz. Using rising edge of the internal sample-clock.\n", NUM_SAMPLES_PER_FRAME, SAMPLE_RATE);
#endif


	/* Make sure the default value of DAQmxSetReadReadAllAvailSamp is (as configured) */
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, READ_AVAIL_SAMPS) );

	/* Commit the task (make sure all hardware is configured and ready). */
	deprintf ("Committing task (%d).\n", DAQmx_Val_Task_Commit);
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) );

	/* outer loop */
	while (1){

		/* Start the task. Sampling will start as soon as we receive the external trigger. */
		deprintf ("Starting task (frame %d)...\n", frame);
		handleErr( DAQmxStartTask(taskHandle) );

		DAQmxIsTaskDone (taskHandle, &isdone); 			/* bugcheck: we know task isn't done, but prove it */
		deprintf (" Is TaskDone (a)?: %d\n", (int)isdone);

		tries=0;
		samples_read_total = 0;
		while (1){

			/* Read of the relevant mode. (in a loop */
			deprintf  ("Reading, in mode as per #defines when compiled...  (frame: %d)\n", frame);
#if SAMPS_TO_READ_AUTO == 1

  #if TIMEOUT_INFINITE == 1
			retval = ( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
  #else
			retval = ( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, 0, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
  #endif

#else

  #if TIMEOUT_INFINITE == 1
			retval = ( DAQmxReadAnalogF64(taskHandle, NUM_SAMPLES_PER_FRAME, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
  #else
			retval = ( DAQmxReadAnalogF64(taskHandle, NUM_SAMPLES_PER_FRAME, 0, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
  #endif

#endif
			samples_read_total += samples_read;
			printf  ("Result: we acquired %d points (total reads this frame: %d, tries: %d), and retval is %d. Frame: %d\n",(int)samples_read, (int)samples_read_total, tries+1, (int)retval, frame);
			handleErr (retval);

			if (samples_read_total >= (int)NUM_SAMPLES_PER_FRAME){
				break;
			}
			tries++;
		}

		/* Stop the task. */
		deprintf ("Stopping task (frame %d).\n", frame);
		handleErr( DAQmxStopTask(taskHandle) );

		frame++;
	}

	/* Done! */
	return 0;
}
