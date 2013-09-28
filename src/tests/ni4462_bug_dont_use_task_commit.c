/* Copyright (C) Richard Neill 2011-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software, You can
 * redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.  An exception is granted to link against 
 * the National Instruments proprietary libraries, such as libnidaqmx. There is NO WARRANTY, neither express nor implied.
 * For the details, please see: http://www.gnu.org/licenses/gpl.html */

/* This illustrates why to NEVER use taskCommit before looping around  {StartTask, DAQmxReadAnalogF64, StopTask}.
   In the code below, this causes AnalogReadF64 to fail probabilistically, in one of two different ways.
   For more details, see: http://forums.ni.com/t5/Multifunction-DAQ/Bug-DAQmxReadAnalogF64-DAQmx-Val-Auto-DAQmx-Val-WaitInfinitely/m-p/1933361

   Note that the use of taskCommit below is correct, (and necessary if we were to be using AC coupling and want the RC circuit to settle). But
   it has the side-effect of great instability. This bug is present in DaqMX 8.0, also in 8.02  (and even verified on Windows).
*/

/* This simplified code demonstrates the bug, in which AnalogReadF64 is broken when wrapped in a tight loop of startTask/stopTask, and either throws an erroneous
   underflow (-200278) error, or returns fewer samples than it must [in blocking mode].

   This can fail in 2 different ways.
      * the DAQmxReadAnalogF64 (DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely) fails to return enough samples  [usually fails on frame 1 or 2, have observed frame 22.]
      * it crashes: DAQmxReadAnalogF64() throws the  -200278 error, even though we KNOW it hasn't underflowed.

The error does NOT occur at the same time every occasion, and varies somewhat depending on ext trigger freq, or values for SAMPLE_RATE.
There is some clustering of the error types.

Build:  gcc -Wall -Wextra  -Werror -O0 -lnidaqmx -o ni4462_bug_dont_use_task_commit ni4462_bug_dont_use_task_commit.c
Run:    for i in `seq 1 100`; do ./ni4462_bug_dont_use_task_commit | tee -a crashlog2.txt ; done

Trigger PFI0 at 1 Hz

Note that the code doesn't always fail within a few seconds; sometimes it runs for several minutes.


Sample errors:

ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 3, Doneness: 0)
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 1, Doneness: 0)
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 1, Doneness: 0)
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 2, Doneness: 0)
CRASHED. Error: -200278. Read failed on frame 2.  Total samples read: 200, Doneness: 0.  (spf: 100, rate: 100.0).
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 1, Doneness: 0)
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 16, Doneness: 0)
ERROR: requested all 100 samples in a single pass with infinite timeout. But only got 2.  (Failed at frame 1, Doneness: 0)


FIXES:
	1. moving the while() to the top and adding a clear-task (i.e. destroying and recreating the task each time) fixes the bug, but takes too long.
	2. usleep(1000000) within the main loop fixes the bug (but takes too long)
	3. Getting rid of the taskcommit works (comment it out below). This is NI's recommendation. But it does make the taskStart()s take a bit longer.
	4  Changing the BLOCKING read to NONBLOCKING Polling reads  (sometimes helps a BIT, not necessary a cure)
	5. Increasing the sample frequency. 100 Hz fails, 10kHz works. (1500 fails, 2000 works). Sample count is irrelevant.


*/

/* Try enabling the workarounds. Set any of these to 1 to make it not crash */
#define WORKAROUND_SLEEP		0
#define WORKAROUND_DONT_COMMIT		0
#define WORKAROUND_NONBLOCK_POLL	0		/* this fix doesn't work */
#define WORKAROUND_SAMPLE_FASTER	0


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
#if WORKAROUND_SAMPLE_FASTER == 0
	float64 SAMPLE_RATE = 100;
#else
	float64 SAMPLE_RATE = 10000;
#endif
int32   he_retval = 0;   		/* Used by #define handleErr() above */
int32   samples_read;			/* Number of samples (per channel) read in this particular call. */
int32   samples_read_thistime;		/* Number of samples (per channel) that were actually read in this pass */
uInt64  samples_read_total = 0;		/* Number of samples (per channel) that have been read so far in (grand) total */
int     frame = 0;
float64 data[(25000 * 4)];		/* Our read data buffer. Multiple of 4. Needn't have room for num_samples_per_frame all at once */
bool32  isdone, isdone2;
char    error_buf[2048]="\0", error_buf2[2048]="\0";


void print_help(char *argv0){		/* Help */
	fprintf (stderr, "This demonstrates why taskCommit is a bad idea, if there is a loop with taskStart...taskStop.\n"
			 "Usage:  %s         (no args)\n"
			 "Trigger PFI0 at 1 kHz.\n"
			 "This should sample forever, %d samples per frame, at a rate of %f Hz. But actually, it crashes,\n"
			 "in one of two ways: by underflow or by task stopping early. This is a bug in libnidaqmx.\n"
			 "For more details, see the source, ni4462_bug_dont_use_task_commit.c\n"
#if WORKAROUND_SLEEP == 1
			"NB this is compiled with '#define WORKAROUND_SLEEP 1'; it shouldn't crash\n"
#elseif WORKAROUND_DONT_COMMIT == 1
			"NB this is compiled with '#define WORKAROUND_DONT_COMMIT 1'; it shouldn't crash\n"
#elseif WORKAROUND_NONBLOCK_POLL == 1
			"NB this is compiled with '#define WORKAROUND_NONBLOCK_POLL 1'; it shouldn't crash\n"
#else
			"NB this is compiled with no workarounds enabled; it should crash as expected.\n"
#endif

			 "\n",
		argv0,(int)NUM_SAMPLES_PER_FRAME,SAMPLE_RATE);
}

void handleErr2 (int error){		/* Error handling: Quit on fatal errors. */

	DAQmxGetErrorString(error, error_buf, sizeof(error_buf));  /* Get long error msg */
	DAQmxGetExtendedErrorInfo (error_buf2, sizeof(error_buf)); /* Get longer error msg */
	deprintf ("-----\nDAQmx Error (%d): %s\n%s\n----\n", error, error_buf, error_buf2);

	/* Is task done ? */
	DAQmxIsTaskDone (taskHandle, &isdone2);
	deprintf ("Is TaskDone(c): %d\n", (int)isdone2);

	printf ("CRASHED. Error: %d. Read failed on frame %d.  Total samples read: %lld, Doneness: %d.  (spf: %lld, rate: %.1f).\n", error, frame, samples_read_total, (int)isdone, NUM_SAMPLES_PER_FRAME, SAMPLE_RATE);
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
	/* Acquire finite number, num_samples_per_frame, of samples (on each channel) at a rate of (coereced)SAMPLE_RATE. */
	handleErr( DAQmxCfgSampClkTiming(taskHandle, 0, SAMPLE_RATE, DAQmx_Val_Rising, DAQmx_Val_FiniteSamps, (NUM_SAMPLES_PER_FRAME) ) );  /* Finite number of samples */
	deprintf ("Acquiring (finite) %lld samples per task. Sample clock requested: %f Hz. Using rising edge of the internal sample-clock.\n", NUM_SAMPLES_PER_FRAME, SAMPLE_RATE);

#if WORKAROUND_NONBLOCK_POLL != 1
	/* Make sure the default value of DAQmxSetReadReadAllAvailSamp is FALSE */
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, FALSE) );
#else
	handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, TRUE) );
#endif

#if WORKAROUND_DONT_COMMIT == 1
	deprintf ("Not committing the task. (Commit is implicit in the first start.\n");
#else
	/* Commit the task (make sure all hardware is configured and ready). */
	//DON'T DO THIS!!  Commenting it out will fix the problem.
	deprintf ("Committing task (%d). This WILL make the program crash later: don't do it.\n", DAQmx_Val_Task_Commit);
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) );
#endif

	/* Main loop, keep acquiring 100 samples every time it is triggered. */
	while (1){

		/* Start the task. Sampling will start as soon as we receive the external trigger. */
		deprintf ("Starting task (frame %d)...\n", frame);
		handleErr( DAQmxStartTask(taskHandle) );

		DAQmxIsTaskDone (taskHandle, &isdone); 			/* bugcheck: we know task isn't done, but prove it */
		deprintf (" Is TaskDone (a)?: %d\n", (int)isdone);

#if WORKAROUND_NONBLOCK_POLL != 1
		/* Blocking read of all the samples that are available. Should block till 100 */
		/* Try both DAQmx_Val_Auto and NUM_SAMPLES_PER_FRAME here, with same result */
		deprintf  ("Blocking read of 100 samples (DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely)...  (frame: %d)\n", frame);
		/* Sometimes fails here... */
		handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
		samples_read_thistime = samples_read;
		samples_read_total += samples_read_thistime;
		deprintf  ("   ...acquired %d points this time.\n",(int)samples_read_thistime);
#else
		deprintf ("Non-blocking read in a polling loop, using DAQmxReadAnalogF64( DAQmx_Val_Auto, 0 ) ... frame: %d\n", frame);
		samples_read_thistime = 0;
		while (1){		/* Must set DAQmxSetReadReadAllAvailSamp = TRUE  above. Otherwise, the nonblocking read returns an error (which we have to ignore). Setting ReadAllAvail=True stops the error */
			handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, 0, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/(sizeof(data[0])*4)), &samples_read, NULL) );
			samples_read_thistime += samples_read;
			if ((unsigned int)samples_read_thistime == NUM_SAMPLES_PER_FRAME){
				break;
			}
		}
		deprintf  ("   ...acquired %d points this time.\n",(int)samples_read_thistime);
#endif

		/* Have we read what we expected */
		if ( (unsigned int)samples_read_thistime != NUM_SAMPLES_PER_FRAME) {
			/* OR fails here */
			printf ("ERROR: requested all %lld samples in a single pass with infinite timeout. But only got %d.  (Failed at frame %d, Doneness: %d)\n",
				NUM_SAMPLES_PER_FRAME, (int)samples_read_thistime, frame, (int)isdone );
			exit (1);
		}

		/* Here, the NI4462 is done. We might have very little time between the last sample being read (i.e. end of this frame), and the start of the next frame. */
		deprintf ("Processing data for frame %d\n", frame); /* do stuff */

		deprintf("Waiting till done\n");  /* For debugging: Just in case, but this doesn't help. We KNOW we're done. */
		handleErr ( DAQmxWaitUntilTaskDone (taskHandle, DAQmx_Val_WaitInfinitely) );

		/* Stop the task. Then loop back, and start again immediately. Don't clear the task; we can go back and start again, using the same configuration. */
		deprintf ("Stopping task (frame %d).\n", frame);
		handleErr( DAQmxStopTask(taskHandle) );

#if WORKAROUND_SLEEP == 1
		/* At this point, a usleep(1000000) helps: it appears to fix the bug. But we can't afford to wait that long. 10000 isn't enough. */
		usleep (1000000);
#endif

		frame++ ;

	}

	/* Done! */
	return 0;
}
