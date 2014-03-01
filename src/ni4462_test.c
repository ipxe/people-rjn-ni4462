/* Test program to use the NI 4462 from National Instruments. See also NI's example:  Acq-IntClk.c
   Uses the internal clock and collects a finite amount of data, then processes it and prints the result. The various channel options can be configured here.

 * Copyright (C) Richard Neill 2011-2013, <ni4462 at REMOVE.ME.richardneill.org>. This program is Free Software. You can
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
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <signal.h>
#include <syslog.h>
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
#define DEV_NAME			"NI 4462"			/* Product name */
#define DEV_DEV				"Dev1"				/* Which PCI device to use. (Can change with PCI slot, even if only 1 NI card is installed) */
#define DEV_TRIGGER_INPUT		"PFI0"				/* Name of the Digital trigger input */
#define DEV_NUM_CH			4				/* Number of input channels on this one. Avoids (some) hardcoded "4"s. */
#define DEV_VALID_VOLTAGE_RANGES	"0.316, 1, 3.16, 10, 31.6, 100"	/* List of +/- voltage input ranges (for help text) */
#define DEV_VOLTAGE_MAX			42.4				/* Max safe input voltage for circut */
#define DEV_VALID_FREQ_RANGE		"31.25 Hz, 204.8 kHz"		/* Range of sampling frequencies supported */
#define DEV_FREQ_MIN			31.25				/*    Min freq */
#define DEV_FREQ_MAX			204800.0			/*    Max freq */
#define DEV_INPUT_IMPEDANCE		"1 M"				/* Device's Zin */
#define DEV_FREQ_QUANTISATION		"181.9 uS/s"			/* Frequency quantisation, to nearest micro sample-per-second */
#define DEV_SAMPLES_MIN			2				/* Min number of samples at a time. (experimentally) */
#define DEV_SAMPLES_MAX			16777215			/* 2^24 - 1, measured experimentally */
#define DEV_PRETRIGGER_SAMPLES_MIN	2				/* Min number of samples before trigger, in reference-trigger mode */
#define DEV_DCAC_SETTLETIME_S		0.782				/* time for the AC coupling to settle: see manual in section: Analog Input Channel Configurations -> Input Coupling */
#define DEV_PREAMP_NEWGAIN_SETTLETIME_S 1.00				/* time for the Pre-amp to settle (-g) if it HAD been saturated, and we are curing this by reducing the gain. Experimentally, this is good. */
#define DEV_ADC_FILTER_DELAY_SAMPLES	63				/* ADC digital filter group delay (standard). (But depends on sample freq if Low-Frequency Alias Rejection Enabled) */

#define DEV_ADCDELAY_EAR_R0		1000				/* Sample-rate vs sample-delay for ADC filter delay, with LF_EAR enabled. */
#define DEV_ADCDELAY_EAR_S0		32	 /* extrapolated */ 	/* The S0 value is extrapolated */
#define DEV_ADCDELAY_EAR_R1		1600				/* Floating point values are rounded: we can't discard half a sample point */
#define DEV_ADCDELAY_EAR_S1		33	/* "32.96875" */	/* _R is sample rate, _S is sample-delay */
#define DEV_ADCDELAY_EAR_R2		3200
#define DEV_ADCDELAY_EAR_S2		34   	/* "33.9375" */
#define DEV_ADCDELAY_EAR_R3		6400
#define DEV_ADCDELAY_EAR_S3		36   	/* "35.875" */
#define DEV_ADCDELAY_EAR_R4		12800
#define DEV_ADCDELAY_EAR_S4		40  	/* "39.75" */
#define DEV_ADCDELAY_EAR_R5		25600
#define DEV_ADCDELAY_EAR_S5		48  	/* "47.5" */
#define DEV_FREQ_MIN_NOEAR		1000				/* Experimental: without EAR, can't sample at < 1kHz */

/* RTSI pins used*/
#define RTSI2 				"RTSI2"				/* ai/ReferenceTrigger  */
#define RTSI3 				"RTSI3"				/* ai/StartTrigger      */
#define RTSI6  				"RTSI6"				/* ai/SampleClock       */
#define RTSI8				"RTSI8"				/* SampleClockTimebase  */
#define RTSI9 				"RTSI9"				/* SyncPulse  */

/* Default value choices. Some of these are paired with a string name. */
#define DEFAULT_CHANNEL			"0"				/* Unless otherwise specifed on command line, defaults are to sample on */
#define DEFAULT_SAMPLE_HZ		200000				/*   Channel_0, DC coupled, Differential mode, at 200 kHz for 10k samples, */
#define DEFAULT_COUNT			10000				/*   using a Rising Edge of the internal trigger, returning the result as a float, in Volts. */
#define DEFAULT_COUNT_STR    	       "10000"
#define DEFAULT_V_LIMIT			10.0
#define DEFAULT_COUPLING 		DAQmx_Val_DC
#define DEFAULT_COUPLING_STR		"dc"
#define DEFAULT_TERMINAL_MODE   	DAQmx_Val_Diff
#define DEFAULT_TERMINAL_MODE_STR	"diff"
#define DEFAULT_TRIGGERING		0
#define DEFAULT_TRIGGERING_STR		"now"
#define DEFAULT_FORMAT_STR		"floatV"
#define DEFAULT_ADCFD_DISCARD_SAMPS	0
#define DEFAULT_REFTRIGGER_SAMPS	0
#define DEFAULT_ENABLE_ADC_LF_EAR	0				/* Bool: Enable low frequency enhanced alias rejection (if on, this makes DEV_ADC_FILTER_DELAY_SAMPLES slightly samplefreq. dependent) */
#define DEFAULT_ENABLE_ADC_LF_EAR_STR	"off"				/* 'on' or 'off' */
#define DEFAULT_INT_CLOCK_EDGE  	DAQmx_Val_Rising		/* Which edge of the internal clock to use? Does it matter? */
#define DEFAULT_INT_CLOCK_EDGE_STR	"re"

/* Buffer sizes */
#define BUFFER_SIZE_TUPLES		25000				/* Max number of data tuples read at a time.  Experimentally, can  be as small as 10, performance is limited below 400; since RAM is plentiful let's say 25k. */
#define BUFFER_SIZE			(BUFFER_SIZE_TUPLES * DEV_NUM_CH) /* Size of the data buffer for all channels (4 channels wide) */

/* Syslog */
#define SYSLOG_IDENTIFIER		"ni4462_test"			/* Prefix for syslog */
#define SYSLOG_USLEEP_US		1000000				/* Delay (us), to try to keep syslog and klog in sequence */

/* Misc */
#define MAX_COMMENTED_DISCARDED_SAMPS	1000				/* keep at most the first 1k discarded samples (-j). */

/* Macros */
#define handleErr(functionCall) if(( he_retval=(functionCall) )){ handleErr2(he_retval); }	/* For each DAQmx call, if retval != 0, handle the error appropriately */
#define dsyslog(...)	if (do_syslog) { syslog (LOG_DEBUG, __VA_ARGS__); usleep (SYSLOG_USLEEP_US); };	/* Syslog, if enabled. Sleep after writing, for correct ordering wrt kernel printk(). */

#define eprintf(...)	fprintf(stderr, __VA_ARGS__)				/* Error printf: send to stderr  */

#define deprintf(...)	if (debug) { fprintf(stderr, __VA_ARGS__); }; dsyslog (__VA_ARGS__);	/* Debug error printf: print to stderr iff debug is set. Syslog if do_syslog is set. */

#define VDEBUG_MAX	100							/* Verbosity-limiting for certain debug messages: limited to 100 in total */
#define vdeprintf(...)	if (vdebugc < VDEBUG_MAX ){ deprintf ( __VA_ARGS__ ); } else if (vdebugc == VDEBUG_MAX) { deprintf ("[Verbosity limiter: maximum %d of these messages.]\n", vdebugc) ; } ; vdebugc++;

#define feprintf(...)	fprintf(stderr, __VA_ARGS__); if (stay_alive){ survived_count++; }else{ exit (EXIT_FAILURE); } ;	/* Fatal error printf: send to stderr and exit. (But, survive if stay_alive.) */
#define ffeprintf(...)	stay_alive=0; feprintf(__VA_ARGS__); 			/* Fatal fatal error (can't keep_alive) */

#define teprintf(...)   if (do_triggerready_delete){ eprintf ( __VA_ARGS__ ); unlink (triggerready_filename); };   /* Clean up trigger file with message. Useful before feprintf(). */

#define outprintf(...)	fprintf(outfile, __VA_ARGS__)				/* Write to output file. */
#define output_1f(...)	fprintf (outfile, "%f\n", __VA_ARGS__);			/* Write 1 formatted data value to output file (as float) */
#define output_4f(...)	fprintf (outfile, "%f\t%f\t%f\t%f\n", __VA_ARGS__);	/* Write 4 columns of data to output file (as float) */
#define output_1d(...)	fprintf (outfile, "%d\n", __VA_ARGS__);			/* Write 1 formatted data value to output file (as int) */
#define output_4d(...)	fprintf (outfile, "%d\t%d\t%d\t%d\n", __VA_ARGS__);	/* Write 4 columns of data to output file (as int) */


/* Globals */
int debug = 0, do_syslog=0;
int stay_alive = 0, survived_count = 0;
int vdebugc = 0;  		/* verbosity limiting debug counter */
int terminate_loop = 0;
char *state = "Initialising";
TaskHandle taskHandle = 0;


/* Show help */
void print_help(char *argv0){
	argv0 = basename(argv0);
	eprintf("This is a simple program for basic usage and experimentation with the National Instruments %s PCI device, configuring the various modes,\n"
		"and capturing some data. The source is highly commented, as example code. Debugging reads settings back from the device.\n"
		"This program creates a DAQmx Analog_Voltage_Input Task, configures the Gain, AC/DC Coupling, Differential Input, Triggering, Count,\n"
		"Sample Frequency, then runs the task, while writing out data. After the task stops, overloads are detected, and statistics are printed.\n"
		"\n"
		"USAGE:  %s  [OPTIONS]  outfile.dat\n"
		"\n"
		"OPTS:  -c  N, all, sum          Capture on channel N (0-3), or all %d, or sum of all %d. [default: %s].\n"
		"       -f  freq          (Hz)   Capture at freq Hz. Range is [%s]. Use -d to find the coerced freq. [default: %d Hz].\n"
		"       -n  N, cont              Capture number of samples (for each channel), or continuously till Ctrl-C. Includes -p, not -j. [default: %d].\n"
		"       -i  ac, dc               Input coupling: AC-coupled, DC-coupled. [default: %s].\n"
		"       -m  diff, pdiff          Terminal Mode: Differential, Pseudodifferential. [default: %s].\n"
		"       -v  v_limit       (V)    Set voltage range for (symmetric) input voltage swing of [-v_limit, +v_limit]. [default: %4.2f V].\n"
		"       -t  fe, re, now          Triggering: Falling-edge, Rising-edge, Start immediately. [default: %s].\n"
		"       -p  N                    Pretriggering: N of the total samples will be acquired before the trigger. Enables 'Reference Trigger'. [default: %d]\n"
		"       -j  N, auto              Junk samples: acquire/discard N extra initial samples. Can compensate for ADC Filter Delay pre-capturing. [default: %d].\n"
		"       -g                       Gain is new/Preamp was saturated. Sleep after setting the gain, to allow a (possibly saturated) preamp to settle.\n"
		"       -o  floatV, int32adc     Set output format: ASCII floating-point-64 in Volts, ASCII int32 in raw ADC-levels. [default: %s].\n"
		"       -l  on, off              Enable NI's 'Low Frequency Enhanced Alias Rejection'. Recommended. [default: %s].\n"
		"       -e  fe, re               Sample on the this edge of the internal clock. Negligible effect. [default: %s]\n"
		"       -T  triggerready_file    When ready for ext-trigger, delete this (pre-created) empty file. Other processes can inotifywait() on it.\n"
		"\n"
		"       -s                       Calculate summary statistics after running (or after Ctrl-C interrupt). Print to stderr.\n"
		"       -b                       Brief output on last-line: rounded std-dev(s), in uV (or ADC-levels, depending on -o). Useful for speech-synth.\n"
		"       -B                       Like -b, but print the mean(s) in mV. Useful for parsing in a pipe. (When combined with -b, this prints first)\n"
		"\n"
		"       -x                       Allow overwriting of existing output file. [default: no].\n"
		"       -d                       Enable verbose debugging. Read back exact values of coerceable settings. Make all warnings fatal.\n"
		"       -A                       Stay Alive. Continue, even after fatal errors (even with -d). Makes voltage-overloads non-fatal.\n"
		"       -D                       Debug log to syslog (identifier: %s, level: log_debug, facility: log_user). For reverse-engineering.\n"
		"       -h                       Show this help message.\n"
		"\n"
		"       -I                       Get detailed information about the card (%s): serial number, DAQmx version, calibration, then exit.\n"
		"       -S                       Self-Calibrate, then exit. This takes about 2.5 minutes, and is not implicit at power-on.\n"
		"       -R                       Reset the device first. Useful to kill any background tasks, or break locks held by another running instance.\n"
		"       -Q                       Reset and then Quit immediately (without acquiring data).\n"
		"\n"
		"NOTES:   * The device is actually more flexible than this program supports: it can combine channels and modes in an arbitrary manner, can\n"
		"            support other input types (IEPE / TEDS sensors), and can use analog level/window triggers.\n"
		"         * The output format is suitable for python's numpy.loadtxt(): multiple columns (Channel 0 on left), of ASCII int/float data, with\n"
		"            comment lines prepended by '#'. Useful for fftplot/linregplot. If outfile is '-', it will be stdout.\n"
		"         * The frequency of the sampling rate is coerced to the nearest %s. Use -d to show actual value.\n"
		"         * The voltage range is coerced to [-x,+x] where x={%s}. Use -d to show actual value. (Max safe input is %.1f V).\n"
		"         * The %s doesn't support configuration of the input impedance; it is fixed at %s.\n"
		"         * When selecting AC coupling, a settling time of %.3f s is added at TaskCommit (see manual: Analog Input Channel Configurations).\n"
		"         * When the pre-amp had been saturated, and we have now reduced the gain, -g delays by %.3f s after TaskCommit. Pre-amp can settle.\n"
		"         * The max (finite) number of samples supported is %d; more will be treated as cont+break, i.e. slightly over-sample and discard.\n"
		"         * Digital filtering in the ADC causes triggers to appear 'early'; up to %d samples can be received before the trigger pulse. (this\n"
		"            value can vary with sample frequency if Low Freq Enhanced Alias Rejection is enabled [default: %s]). Use: '-j auto'.\n"
		"         * Numbering: -n includes -p but excludes -j. E.g. \"-n500 -p100 -j20\" acquires 500 data-points, of which 80 precede the trigger pulse,\n"
		"            (discarding 20 preceeding points). This assumes that the filter-delay has also been externally-compensated by a delay-line on %s.\n"
		"            For compensation without a delay-line (at high-frequency, eg %d Hz), use '-p%d -j%d', or '-p%d j%d', and set -n to what you want.\n"
		"         * To view output, use dat2wav (convert to wav file), linregplot (plot linear regression), fftplot (plot fft spectrum).\n"
		"         * RTSI and clock outputs are:\n"
		"            - %s ai/ReferenceTrigger: 25 ns _-_ pulse on reference trigger start. \n"
		"            - %s ai/StartTrigger: 25 ns _-_ pulse on acquisition start. \n"
 		"            - %s ai/SampleClock: pulse train at sample freq. Ref_trigger starts this immediately, else silent till trigger; then N pulses.\n"
		"            - %s SampleClockTimebase: constant clock at the oversampled rate, 128x f_sample.\n"
		"            - %s SyncPulse: very short pulse to sync multiple NI4462s.\n"
		"            - clock output: 100 MHz extracted directly from Crystal oscillator via LVDS (FIN1001)\n"
		"         * The PulseBlaster's HW_Trigger is gated by %s (D flip-flop) to avoid jitter; must operate in reference-trigger mode.\n"
		"         * The filter-delay can be compensated by externally delaying the trigger pulse to %s; use arduino_delay to control the delay-line.\n"
		"\n"
		"SIGNALS: * SigINT (Ctrl-C) cleanly stops sampling at end of loop; SigQUIT (Ctrl-\\) quits immediately\n"
		"         * SigUSR1 prints state to stderr: Initialising, Calibrating, Configuring, Committing, Committed, Ready/Running, Running, Stopping, Stopped.\n"
		"         * IPC: external process should create empty tempfile, use -T. Then wait for deletion ('inotifywait -e delete'), before sending trigger pulse'.\n"
		"\n"
		"ERRORS:  * The following errors are detected and handled: invalid/out-of-range configuration, input voltage overload (pre+post digitisation),\n"
		"           sample-buffer underflow (error: -200278) or overflow (error: -200279), locking i.e. device already in use (error: -50103), R\n"
		"         * Missed triggering (i.e. a 2nd trigger arrives before the task completes) is NOT detected.\n"
		"\n"
		"DOCS:    * NI Dynamic Signal Acquisition User Manual, NI 446x Specifications, /usr/local/natinst/nidaqmx/docs/cdaqmx.chm/_main.html (C library).\n"
		"         * NOTES.txt ( /usr/local/share/doc/ni4462 ).\n"
		"\n"
		,DEV_NAME, argv0, DEV_NUM_CH, DEV_NUM_CH, DEFAULT_CHANNEL, DEV_VALID_FREQ_RANGE, DEFAULT_SAMPLE_HZ, DEFAULT_COUNT, DEFAULT_COUPLING_STR,
		DEFAULT_TERMINAL_MODE_STR, DEFAULT_V_LIMIT, DEFAULT_TRIGGERING_STR, DEFAULT_ADCFD_DISCARD_SAMPS, DEFAULT_REFTRIGGER_SAMPS, DEFAULT_FORMAT_STR,
		DEFAULT_ENABLE_ADC_LF_EAR_STR, DEFAULT_INT_CLOCK_EDGE_STR, DEV_DEV, SYSLOG_IDENTIFIER, DEV_FREQ_QUANTISATION, DEV_VALID_VOLTAGE_RANGES, DEV_VOLTAGE_MAX, DEV_NAME, DEV_INPUT_IMPEDANCE,
		DEV_DCAC_SETTLETIME_S, DEV_PREAMP_NEWGAIN_SETTLETIME_S, DEV_SAMPLES_MAX, DEV_ADC_FILTER_DELAY_SAMPLES, DEFAULT_ENABLE_ADC_LF_EAR_STR, DEV_TRIGGER_INPUT, 
		DEFAULT_SAMPLE_HZ, 0, DEV_ADC_FILTER_DELAY_SAMPLES, 2, (2+DEV_ADC_FILTER_DELAY_SAMPLES),  RTSI2, RTSI3, RTSI6, RTSI8, RTSI9, RTSI6, DEV_TRIGGER_INPUT);
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
		fprintf (stderr, "DAQmx Warning: %s\n\n%s\n\n", error_buf, error_buf2);
	}
	return;
}


/* Get information about the system */
void get_info(){
	int32  he_retval = 0;
	uInt32 product_num, serial_num, daqmx_major, daqmx_minor, year, month, day, hour, minute;
	eprintf ("Information about device %s (%s):\n", DEV_NAME, DEV_DEV);

	/* Get product and serial numbers. */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func231d.html , /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func0632.html */
	deprintf ("Getting Product and Serial numbers...\n");
	handleErr( DAQmxGetDevProductNum(DEV_DEV, &product_num) );
	handleErr( DAQmxGetDevSerialNum(DEV_DEV, &serial_num) );
	eprintf ("Product number: %u, Serial number: %u\n", (unsigned int)product_num, (unsigned int)serial_num);

	/* Get DAQmx version info */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func1272.html , /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func1923.html */
	deprintf ("Getting DAQmx version...\n");
	handleErr( DAQmxGetSysNIDAQMajorVersion(&daqmx_major) );
	handleErr( DAQmxGetSysNIDAQMinorVersion(&daqmx_minor) );
	eprintf ("DAQmx version: %u.%u\n", (unsigned int)daqmx_major, (unsigned int)daqmx_minor);

	/* Get time of last External Calibration */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxgetextcallastdateandtime.html */
	deprintf ("Getting Last External Calibration date and time...\n");
	handleErr ( DAQmxGetExtCalLastDateAndTime (DEV_DEV, &year, &month, &day, &hour, &minute) );
	eprintf ("Last External Calibration was at: %d-%02d-%02d %02d:%02d.\n", (unsigned int)year, (unsigned int)month, (unsigned int)day, (unsigned int)hour, (unsigned int)minute);

	/* Get time of last Self Calibration */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxgetselfcallastdateandtime.html */
	deprintf ("Getting Last Self Calibration date and time...\n");
	handleErr ( DAQmxGetSelfCalLastDateAndTime (DEV_DEV, &year, &month, &day, &hour, &minute) );
	eprintf ("Last Self     Calibration was at: %d-%02d-%02d %02d:%02d.\n", (unsigned int)year, (unsigned int)month, (unsigned int)day, (unsigned int)hour, (unsigned int)minute);
}


/* Do self calibration */
void self_calibrate(){
	int32  he_retval = 0;
	uInt32 year, month, day, hour, minute;

	/* Get time of previous Self Calibration */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxgetselfcallastdateandtime.html */
	deprintf ("Getting Last Self Calibration time...\n");
	handleErr( DAQmxGetSelfCalLastDateAndTime (DEV_DEV, &year, &month, &day, &hour, &minute) );
	eprintf  ("Previous Self Calibration was at: %d-%02d-%02d %02d:%02d.\n", (unsigned int)year, (unsigned int)month, (unsigned int)day, (unsigned int)hour, (unsigned int)minute);

	/* Do self calibration */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxselfcal.html */
	eprintf  ("Performing Self Calibration now...\n");
	deprintf ("Performing Self Calibration now...\n");
	state = "Calibrating";
	handleErr( DAQmxSelfCal (DEV_DEV) );
	eprintf  ("Done\n");
}


/* Reset the device. */
void reset_device(){
	int32  he_retval = 0;

	/* Reset the device. Useful to kill any orphaned tasks. */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxresetdevice.html */
	eprintf  ("Resetting the device now...\n");
	deprintf ("Resetting the device [ and undoing persistent DAQmxConnectTerms() ]...\n");
	handleErr( DAQmxResetDevice (DEV_DEV) );
	eprintf  ("Done\n");
}


/* Signal handler: handle Ctrl-C in middle of main loop. */
void handle_signal_cc(int signum){
	eprintf ("Ctrl-C (sig %d), stopping acquiring samples.\n", signum);
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
	int	do_stats = 0, do_brief_mean = 0, do_brief_sd = 0, do_getinfo = 0, do_selfcal = 0, do_reset = 0, do_resetandquit = 0, do_triggerready_delete = 0;
	int 	num_channels = 1, sum_channels = 0, allow_overwrite = 0, continuous = 0;
	int     format_floatv = 1, adcdelay_discard_auto = 0, need_to_settle_dcac = 0, need_to_settle_newgain = 0;
	uInt64  num_samples = DEFAULT_COUNT;
	int	input_coupling =  DEFAULT_COUPLING;
	int	terminal_mode = DEFAULT_TERMINAL_MODE;
	int     trigger_ext = DEFAULT_TRIGGERING;
	int	trigger_edge = DEFAULT_TRIGGERING;
	int	clock_edge = DEFAULT_INT_CLOCK_EDGE;
	char   *input_channel = "/"DEV_DEV"/ai"DEFAULT_CHANNEL;
	char   *samplenum_arg = DEFAULT_COUNT_STR;
	char   *channel_arg = DEFAULT_CHANNEL;
	char   *coupling_arg = DEFAULT_COUPLING_STR;
	char   *terminal_arg = DEFAULT_TERMINAL_MODE_STR;
	char   *triggering_arg = DEFAULT_TRIGGERING_STR;
	char   *edge_arg = DEFAULT_INT_CLOCK_EDGE_STR;
	char   *format_arg = DEFAULT_FORMAT_STR;
	char   *discard_arg  __attribute__ ((unused)) = DEFAULT_ADCFD_DISCARD_SAMPS;
	char   *reftrigger_arg  __attribute__ ((unused)) = DEFAULT_REFTRIGGER_SAMPS;
	int     enable_adc_lf_ear = DEFAULT_ENABLE_ADC_LF_EAR;
	char   *enable_adc_lf_ear_arg = DEFAULT_ENABLE_ADC_LF_EAR_STR;
	int     adcdelay_discard_samples = DEFAULT_ADCFD_DISCARD_SAMPS;
	float64 vin_min = - DEFAULT_V_LIMIT, vin_max = DEFAULT_V_LIMIT;
	float64 sample_rate = DEFAULT_SAMPLE_HZ;
	int32   he_retval = 0;   			/* Used by #define handleErr() above */
	float64 readback_hz = 0, readback_v1 = 0, readback_v2 = 0, readback_g;
	int32   readback_c = 0, readback_t = 0;
	uInt32  readback_input_buf_size, readback_onboard_buf_size, pretrigger_samples = 0;
	bool32  overload_occurred = 0, enh_alias_reject = 0;
	int32   num_samples_read_thistime;		/* Number of samples (per channel) that were actually read in this pass */
	int     num_samples_printed = 0;
	uInt64  num_samples_read_total = 0;		/* Number of samples (per channel) that have been read so far in total */
	float64 data[BUFFER_SIZE], data_tmp;		/* Our read data buffer. Multiple of 4. Needn't have room for num_samples all at once */
	int32   data_i[BUFFER_SIZE], data_i_tmp;	/* Equvalent, when reading as int32 in ADC levels. [todo: could save some RAM by using a union of (data,datai)]. */
	int     i, j, uvx, mvx, ret, tmp, n, m;
	float64	sum[DEV_NUM_CH]={0,0,0,0}, sum_squares[DEV_NUM_CH]={0,0,0,0}, mean[DEV_NUM_CH], mean_s, var[DEV_NUM_CH], stddev[DEV_NUM_CH], stddev_s;
	struct  stat stat_p;            		/* pointer to stat structure */
	char   *output_filename;			/* output file */
	char   *triggerready_filename = "";		/* trigger_ready filename */
	FILE   *outfile = stdout;
	char    *mv, *uv;
	char    error_buf[2048]="\0";
	struct  timeval	then, now;

	/* Set handler for SIGUSR1: print state to stderr. */
	signal(SIGUSR1, handle_signal_usr1);

	/* Parse options and check for validity */
        if ((argc > 1) && (!strcmp (argv[1], "--help"))) {      /* Support --help, without the full getopt_long */
                print_help(argv[0]);
                exit (EXIT_SUCCESS);
        }

        while ((opt = getopt(argc, argv, "sdbghxABDIQRSc:e:f:i:j:l:m:n:o:p:t:v:T:")) != -1) {  /* Getopt */
                switch (opt) {
                        case 'h':                               /* Help */
				print_help(argv[0]);
				exit (EXIT_SUCCESS);
				break;
			case 'A':				/* Stay alive, even with fatal errors.*/
				stay_alive = 1;
				break;
			case 'I':				/* Get info */
				do_getinfo = 1;
				break;
			case 'S':				/* Do self calibration */
				do_selfcal = 1;
				break;
			case 'R':				/* Reset the device. */
				do_reset = 1;
				break;
			case 'Q':				/* Reset the device and quit. */
				do_resetandquit = 1;
				break;
			case 'D':				/* Debug to syslog. */
				do_syslog = 1;
				break;
			case 'd':				/* Debugging enabled */
				debug = 1;
				break;
			case 's':				/* Summary stats */
				do_stats = 1;
				break;
			case 'b':				/* Brief std-dev output */
				do_brief_sd = 1;
				break;
			case 'B':				/* Brief mean output */
				do_brief_mean = 1;
				break;
			case 'g':				/* Gain is changed, preamp Was saturated; settle the preamp */
				need_to_settle_newgain = 1;
				break;
			case 'x':				/* Allow output file to be overwritten if it exists */
				allow_overwrite = 1;
				break;

			case 'c':				/* Input Channel: 0,1,2,3,all,sum */
				channel_arg = optarg;
				if (!strcasecmp(optarg, "0")){
					input_channel = "/"DEV_DEV"/ai0";
				}else if (!strcasecmp(optarg, "1")){
					input_channel = "/"DEV_DEV"/ai1";
				}else if (!strcasecmp(optarg, "2")){
					input_channel = "/"DEV_DEV"/ai2";
				}else if  (!strcasecmp(optarg, "3")){
					input_channel = "/"DEV_DEV"/ai3";
				}else if  (!strcasecmp(optarg, "all")){
					input_channel = "/"DEV_DEV"/ai0:3";
					num_channels = DEV_NUM_CH;
				}else if (!strcasecmp(optarg, "sum")){
					input_channel = "/"DEV_DEV"/ai0:3";
					num_channels = DEV_NUM_CH; sum_channels = 1;
				}else{
					feprintf ("Fatal Error: unrecognised channel: %s\n", optarg);
				}
				break;

			case 'e':				/* Internal sampling edge: rising-edge/falling-edge */
				edge_arg = optarg;
				if (!strcasecmp(optarg, "fe")){
					clock_edge = DAQmx_Val_Falling;
				}else if (!strcasecmp(optarg, "re")){
					clock_edge = DAQmx_Val_Rising;
				}else{
					feprintf ("Fatal Error: unrecognised sample edge: %s\n", optarg);
				}
				break;

			case 'f':				/* Sample Frequency. Float64 */
				sample_rate = strtod(optarg, NULL);
				if ( (sample_rate < DEV_FREQ_MIN) || (sample_rate > DEV_FREQ_MAX) ){  /* Limits of device */
					feprintf ("Fatal Error: sample rate (-f) must be between %f and %f Hz.\n", DEV_FREQ_MIN, DEV_FREQ_MAX);
				}
				break;

			case 'i':				/* Input Coupling: AC, DC.  */
				coupling_arg = optarg;
				if (!strcasecmp(optarg, "ac")){
					input_coupling = DAQmx_Val_AC;
				}else if (!strcasecmp(optarg, "dc")){
					input_coupling = DAQmx_Val_DC;
				}else{	/* DAQmx_Val_GND is NOT supported by this device */
					feprintf ("Fatal Error: unrecognised input coupling: %s\n", optarg);
				}
				break;

			case 'j':				/* ADC delay: discard N samples? May be 0, N or auto */
				discard_arg = optarg;
				if (!strcasecmp(optarg, "auto")){
					adcdelay_discard_auto = 1;
				}else{
					adcdelay_discard_samples = atoi(optarg);
					if (adcdelay_discard_samples < 0){
						feprintf ("Fatal Error: adc delay discard samples (-j) must be >= 0\n");
					}
				}
				break;

			case 'l':				/* Enable Low Frequency Enhanced Antialiasing.  */
				enable_adc_lf_ear_arg = optarg;
				if (!strcasecmp(optarg, "on")){
					enable_adc_lf_ear = 1;
				}else if (!strcasecmp(optarg, "off")){
					enable_adc_lf_ear = 0;
				}else{
					feprintf ("Fatal Error: unrecognised value for -l: %s\n", optarg);
				}
				break;

			case 'm':				/* Terminal Configuration: Differential, Pseudodifferential, [Referenced Single-Ended], [Non-Referenced S.E.],  */
				terminal_arg = optarg;
				if (!strcasecmp(optarg, "diff")){
					terminal_mode = DAQmx_Val_Diff;
				}else if  (!strcasecmp(optarg, "pdiff")){
					terminal_mode = DAQmx_Val_PseudoDiff;
				}else{	/* DAQmx_Val_RSE and DAQmx_Val_NRSE are not supported by this device */
					feprintf ("Fatal Error: unrecognised terminal configuration: %s\n", optarg);
				}
				break;

			case 'n':				/* Number of samples to capture. Integer, or "cont". */
				samplenum_arg = optarg;
				if (!strcasecmp(optarg, "cont")){
					num_samples = 0;
					continuous = 1;
				}else{
					num_samples = strtoll (optarg, NULL, 10);
					if (num_samples <= 0){
						feprintf ("Fatal Error: number_of_samples (-n) must be > 0.\n");
					}else if (num_samples < DEV_SAMPLES_MIN){
						feprintf ("Fatal Error: number_of_samples (-n) must be >= %d.\n", DEV_SAMPLES_MIN);
					}
				}
				break;

			case 'o':				/* Output format: ASCII floating-point64 in Volts or int32 in ADC-units*/
				format_arg = optarg;
				if (!strcasecmp(optarg, "floatV")){
					format_floatv = 1;
				}else if (!strcasecmp(optarg, "int32adc")){
					format_floatv = 0;
				}else{
					feprintf ("Fatal Error: unrecognised output format: %s\n", optarg);
				}
				break;

			case 'p':				/* Reference trigger: number of pretrigger samples to capture. */
				reftrigger_arg = optarg;	/* Don't allow an explicit "-p 0" to disable this; reftrigger mode ALSO enables RTSI6 clock sync. */
				tmp = strtoll (optarg, NULL, 10);
				if (tmp < DEV_PRETRIGGER_SAMPLES_MIN){
					feprintf ("Fatal Error: in reference-trigger mode, number of pretrigger samples (-p) must be >= %d.\n", DEV_PRETRIGGER_SAMPLES_MIN);
				}
				pretrigger_samples = tmp;
				break;

			case 't':				/* Triggering: rising-edge, falling-edge, start immediately */
				triggering_arg = optarg;
				if (!strcasecmp(optarg, "fe")){
					trigger_ext = 1;
					trigger_edge = DAQmx_Val_Falling;
				}else if (!strcasecmp(optarg, "re")){
					trigger_ext = 1;
					trigger_edge = DAQmx_Val_Rising;
				}else if (!strcasecmp(optarg, "now")){
					trigger_ext = 0;
				}else{
					feprintf ("Fatal Error: unrecognised triggering mode: %s\n", optarg);
				}
				break;

			case 'v':				/* Voltage Limit: set gain/rainge for input voltage swing of [-v_limit, +v_limit] */
				vin_max = atof(optarg);		/* The device will coerce vin_min == - vin_max whether or not we want it. */
				vin_min = - vin_max;
				if (vin_max > DEV_VOLTAGE_MAX){
					feprintf ("Fatal Error: voltage must be <=  +/- %f V (limitations of device input.)\n", DEV_VOLTAGE_MAX);
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

	if (do_syslog){	/* Syslog. Open now, don't wait. */
		openlog (SYSLOG_IDENTIFIER, LOG_NDELAY, LOG_USER);
		dsyslog ("Starting program 'ni4462_test...\n");
		deprintf("Writing everthing to syslog, as the identifier '%s'\n", SYSLOG_IDENTIFIER);
	}
	if (do_getinfo){
		get_info();
		exit (EXIT_SUCCESS);
	}
	if (do_selfcal){
		reset_device();
		self_calibrate();
		exit (EXIT_SUCCESS);
	}
	if (do_resetandquit){
		reset_device();
		exit (EXIT_SUCCESS);
	}
	if (do_reset){
		reset_device();
		/* Don't exit here */
	}

	if (argc - optind != 1){		/* Output file. Must be specified. May be "-" for stdout. */
		if (do_reset){			/* [Trivial case: catch user-error invocation with just -R, probably meant -RQ, should quit without complaint.] */
			exit (EXIT_SUCCESS);
		}
		ffeprintf ("This takes exactly one non-optional argument, the output file. Use -h for help.\n");
	}
	output_filename = argv[optind];

	if (!strcmp(output_filename, "-")){
		outfile = stdout;
	}else if ( (!allow_overwrite) && (strcmp(output_filename, "/dev/null")) && (stat (output_filename, &stat_p) != -1) ){	/* check we can't stat, i.e. doesn't exist. */
		ffeprintf ("Output file '%s' already exists, and -x was not specified. Will not overwrite.\n", output_filename);
	}else if ( ( outfile = fopen ( output_filename, "w" ) ) == NULL ) {   /* open for writing and truncate */
		ffeprintf ("Could not open %s for writing: %s\n", output_filename, strerror ( errno ) );
	}
	
 	if (do_triggerready_delete){		/* Trigger Ready file must pre-exist, and be empty. */
 		if (stat (triggerready_filename, &stat_p) == -1){
 			ffeprintf ("Trigger-Ready signal-file '%s' doesn't exist. It must be pre-created (empty) by the external process and supplied to us.\n", triggerready_filename);
 		}else if (stat_p.st_size > 0){
 			ffeprintf ("Trigger-Ready signal-file '%s' isn't empty. Will not accidentally delete something containing data.\n", triggerready_filename);
 		}
 	}

	if (num_samples > (DEV_SAMPLES_MAX - DEV_ADC_FILTER_DELAY_SAMPLES) ){  /* Too many samples for finite mode. Promote to cont. (This workaround is imperfect: more complex, and doesn't necessarily end promptly). Also broken with ref-trigger. */
		eprintf ("Note: number of samples must usually be <= %d  (this program also requires a %d margin for worst-case ADC filter delay). Changing to continuous mode with break.\n", DEV_SAMPLES_MAX, DEV_ADC_FILTER_DELAY_SAMPLES);
		continuous = 1;
	}

	if (pretrigger_samples > 0 && continuous == 1){ /* Pretrigger samples are *included* in the total, can only be used in Finite mode. */
		teprintf("Deleting Trigger-Ready signal file before fatal error: fake the trigger to prevent hang.\n");  /* Before exiting, fake the trigger-ready signal if appropriate. This ensures other processes don't wait forever. */
		feprintf ("Fatal Error: pretrigger samples (reference trigger) can only be used in finite, rather than continuous mode.\n");
	}else if ((pretrigger_samples >= num_samples) && (continuous != 1)){
		teprintf("Deleting Trigger-Ready signal file before fatal error: fake the trigger to prevent hang.\n");  /* Fake trigger, prevent hang. As above */
		feprintf ("Fatal Error: the number of pretrigger samples (%u) is *included* within the total number of samples (%lld), so must be smaller.\n", (unsigned int)pretrigger_samples, (unsigned long long)num_samples);
	}

	if (format_floatv == 0){  /* Noise performance warning */
		deprintf ("Selected int32adc format: warning, this is sometimes (unexpectedly) noisier than floatV: see NOTES.txt\n");
	}

	if (!enable_adc_lf_ear && sample_rate < DEV_FREQ_MIN_NOEAR){
		teprintf("Deleting Trigger-Ready signal file before fatal error: fake the trigger to prevent hang.\n");  /* Fake trigger, prevent hang. As above */
		feprintf ("Fatal Error: sample rate (%f Hz) cannot be below %d Hz unles Enhanced Low Freq Alias Rejection (-l) is enabled.\n", sample_rate, DEV_FREQ_MIN_NOEAR);  //Do we want
	}


	/* Look up ADC filter delay samples to discard. See table "ADC Filter Delay" in "NI 446x Specifications datasheet". */
	if (adcdelay_discard_auto){	/* Auto */
		if (enable_adc_lf_ear){	/* Enhanced Low Frequency Antialiasing...makes the number of samples depend on freq. */
			if (sample_rate <= DEV_ADCDELAY_EAR_R0){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S0;	/* We have to make these integers, but they shouldn't be */
			}else if (sample_rate <= DEV_ADCDELAY_EAR_R1){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S1;
			}else if (sample_rate <= DEV_ADCDELAY_EAR_R2){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S2;
			}else if (sample_rate <= DEV_ADCDELAY_EAR_R3){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S3;
			}else if (sample_rate <= DEV_ADCDELAY_EAR_R4){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S4;
			}else if (sample_rate <= DEV_ADCDELAY_EAR_R5){
				adcdelay_discard_samples = DEV_ADCDELAY_EAR_S5;
			}else{
				adcdelay_discard_samples =  DEV_ADC_FILTER_DELAY_SAMPLES;
			}
		}else{			/* No Enhanced LF AA.. fixed value */
			adcdelay_discard_samples = DEV_ADC_FILTER_DELAY_SAMPLES;
		}
		if (pretrigger_samples != 0){   /* helpful warning that -j auto -p 2  won't compensate! */
			deprintf("ADC filter delay set (via 'auto') to %d, with non-zero pretrigger_samples %u. %u samples will be acquired pre-trigger (rather than exact compensation.)\n", adcdelay_discard_samples, (unsigned int)pretrigger_samples, (unsigned int)pretrigger_samples)
		}
	}
	if (adcdelay_discard_samples == 0){
		deprintf ("ADC filter delay: zero samples being discarded: *beware* of digital filter group delay!\n");
	}else{
		deprintf ("Compensate for ADC digital filter group delay / 'pre-capturing': will discard the first %d samples (%.3f ms). LF_EAR is %s.\n",adcdelay_discard_samples, (1000 * (float)adcdelay_discard_samples / sample_rate), enable_adc_lf_ear_arg );
		if (adcdelay_discard_samples != DEV_ADC_FILTER_DELAY_SAMPLES){
			deprintf ("Note: the exactly correct time delay actually corresponds to a non-integer number of samples, but that would be impossible to implement.\n");
		}
	}

	/* Now create and configure the DAQmx task. */
	gettimeofday(&then, NULL);

	/* Create Task. Function call takes ~ 30 ms. (The name is arbitrary, but in some cases, setting "" leaks memory) */
	deprintf ("DAQmxCreateTask: creating new task...\n");
	state = "Configuring";
	handleErr( DAQmxCreateTask("TestTask",&taskHandle) );

	/* Connect Terminals. This connects RTSI output pins to signal sources within the device. This is immediate routing, not task based, and persists (even across instances of the program) till explicit unexport or device reset*/
	/* Must be called before taskConfiguration eg Voltage range setup. Error -89137 arises if we have aready used a route or signal, and haven't cleared it (eg using Reset) */
	/* handleErr() does produce helpful, specific errors if we attempt a routing that the device doesn't support, or use an illegal terminal name. Note that the "paths" must be fully qualified. */
	/* Documented at:  /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxconnectterms.html and  http://zone.ni.com/reference/en-XX/help/371893D-01/6536and6537help/exporting_clocks/  and ftp://ftp.ni.com/support/daq/pc/ni-daqmxbase/1.4/Readme.htm */

	/* Experimentally:
	 * - DAQmx_Val_InvertPolarity is never supported in the 4462; always use DAQmx_Val_DoNotInvertPolarity.
	 * - The following signals can't be exported (to any RTSI pin) by this hardware: MasterTimebase, 100kHzTimebase, 20MHzTimebase, OnboardClock, ai/ConvertClock, ai/ConvertClockTimebase, aiSampleClockTimebase
	 * - The RTSI_OSC terminal doesn't exist (despite being shown on the connector pinout)
	 * - SampleClockTimebase (typ: 25.6 MHz) and DividedSampleClockTimebase (type: 12.3 MHz) (the oversample-clock or half-the-oversample clock) can only be sent to RTSI8. http://digital.ni.com/public.nsf/allkb/A133ED27DF9BCC5986256F2E004BA342
         * - SyncPulse can only be routed to RTSI9  (but this doesn't actually seem visible on the RTSI connector)
         * - There is no way to export the master 100 MHz clock...except with a soldering iron: http://forums.ni.com/t5/Multifunction-DAQ/How-to-get-a-regular-clock-10-MHz-20-MHz-or-100-MHz-from-the-NI/td-p/1969719
	*/
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/ai/ReferenceTrigger", "/"DEV_DEV"/"RTSI2,  DAQmx_Val_DoNotInvertPolarity));	/* Can be any of 0-6 */
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/ai/StartTrigger", "/"DEV_DEV"/"RTSI3,  DAQmx_Val_DoNotInvertPolarity));   	/* Can be any of 0-6 */
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/ai/SampleClock", "/"DEV_DEV"/"RTSI6,  DAQmx_Val_DoNotInvertPolarity));  	/* Can be any of 0-6. Important: this one (on 6) is hardwired to the pulseblaster's HW_TRIG */
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/SampleClockTimebase", "/"DEV_DEV"/"RTSI8,  DAQmx_Val_DoNotInvertPolarity)); /* 128x Oversample clock, can only be RTSI8. [Alt: output DividedSampleClockTimebase (half speed) */
	handleErr ( DAQmxConnectTerms ( "/"DEV_DEV"/SyncPulse",  "/"DEV_DEV"/"RTSI9,  DAQmx_Val_DoNotInvertPolarity));   	/* Only RTSI9 allowed. What IS this?  Can't invert. Can't find it on the connector! */


	/* Configure a Voltage input chanel for this task. Function call takes ~ 750 ms. */
	/* Set input channels: choose the channel(s), the terminal-mode, and the voltage-scale. NB the voltage scale is coerced by device capabilities. */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxcreateaivoltagechan.html */
	deprintf ("DAQmxCreateAIVoltageChan: setting input_channel to %s, terminal_mode to %s, and voltage range to [%f, %f] ...\n", input_channel,
				terminal_mode == DAQmx_Val_Diff ?  "DAQmx_Val_Diff" : ( terminal_mode == DAQmx_Val_PseudoDiff ? "DAQmx_Val_PseudoDiff" : "Invalid" ), vin_min, vin_max);
	handleErr( DAQmxCreateAIVoltageChan(taskHandle, input_channel, "VoltageInput", terminal_mode, vin_min, vin_max, DAQmx_Val_Volts, NULL) );  /* DAQmx_Val_Volts is the scale-type */
 	handleErr( DAQmxGetAIMin (taskHandle, input_channel, &readback_v1)  ); 	/* Check coercion */
 	handleErr( DAQmxGetAIMax (taskHandle, input_channel, &readback_v2) );
	handleErr( DAQmxGetAIGain(taskHandle, input_channel, &readback_g) );
	deprintf ("Input Voltage range requested: [%f, %f] V; actually coerced by device to: [%f, %f] V. Gain is: %f dB.\n", vin_min, vin_max, readback_v1, readback_v2, readback_g);
	handleErr( DAQmxGetAITermCfg (taskHandle, input_channel, &readback_t) );
	deprintf ("Input terminal_mode: readback %d\n", (int)readback_t);


	/* Configure Channel input-coupling (AC/DC). Values may be DAQmx_Val_AC   DAQmx_Val_DC. [DAQmx_Val_GND not supported]. Function call takes ~ 0.4 ms. */
	/* Check whether we are swapping from DC to AC coupling. If so, pause LATER to allow it to settle (see manual, in section: Analog Input Channel Configurations */
 	/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/attr0064.html */
	handleErr( DAQmxGetAICoupling (taskHandle, input_channel, &readback_c) );
	deprintf ("DAQmxSetAICoupling: setting input_coupling to %d, %s ...\n", input_coupling,
				( input_coupling == DAQmx_Val_AC ?  "DAQmx_Val_AC" : ( input_coupling == DAQmx_Val_DC ?  "DAQmx_Val_DC" : "Invalid" ))  );
	handleErr( DAQmxSetAICoupling (taskHandle, input_channel, input_coupling) );
	if  ( (input_coupling == DAQmx_Val_AC) &&  (readback_c == DAQmx_Val_DC) ){
		need_to_settle_dcac = 1;  /* Wait till commit task; the change isn't actually applied to the hardware yet. */
		/* Very annoyingly, it seems that setting the channel to AC never persists across instances of this program, even if we don't do stoptask/cleartask. OR. maybe
		 * DAQmxCreateAIVoltageChan() defaults back to DC every time. So this delay happens every single time, not just if we had previously been using the device in DC-coupled mode. */
	}
	handleErr( DAQmxGetAICoupling (taskHandle, input_channel, &readback_c) );
	deprintf ("Input coupling: readback %d\n", (int)readback_c);


	/* Configure Channel Impedance: 50,75,1M,10G. Value is in Ohms. */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/attr0062.html */
	/* NOT supported by this device. The NI4462 has a fixed 1M input impedance. */
	/* [Similarly, this device also does NOT support: DAQmxSetAIAutoZeroMode(), DAQmxSetDelayFromSampClkDelay(), DAQmxSetDelayFromSampClkDelayUnits() ] */
	// eprintf  ("DAQmxSetAIImpedance: setting input_impedance to %f ...\n", input_impedance);
 	// handleErr( DAQmxSetAIImpedance (taskHandle, input_channel, input_impedance) );	/* Do it */


	/* Configure Triggering. Set trigger input to digital triggering via the external SMA connector, and select the edge. Function call takes ~ 0.2 ms. */
	/* Note: this program can only be triggered once, then it runs and exits. To make it retriggerable, just loop around (start task... stop task) */
	/* Documented at /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxcfgdigedgestarttrig.html */
	/* Alternatively, we can use a "reference trigger", and acquire samples pre-triggering. */
	/* Documented at /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxcfgdigedgereftrig.html */
	if (trigger_ext){
		if (pretrigger_samples == 0){
			deprintf ("DAQmxCfgDigEdgeStartTrig: setting triggering to external trigger input, %s, using edge: %s ...\n", "/"DEV_DEV"/"DEV_TRIGGER_INPUT,
					( trigger_edge == DAQmx_Val_Falling ?  "DAQmx_Val_Falling" : ( trigger_edge == DAQmx_Val_Rising ?  "DAQmx_Val_Rising" : "Invalid" ))  );
			handleErr( DAQmxCfgDigEdgeStartTrig (taskHandle, "/"DEV_DEV"/"DEV_TRIGGER_INPUT, trigger_edge) );
		}else{
			deprintf ("DAQmxCfgDigEdgeRefTrig: setting reference trigger on external trigger input, %s, using edge: %s, with %u pre-trigger samples ...\n", "/"DEV_DEV"/"DEV_TRIGGER_INPUT,
					( trigger_edge == DAQmx_Val_Falling ?  "DAQmx_Val_Falling" : ( trigger_edge == DAQmx_Val_Rising ?  "DAQmx_Val_Rising" : "Invalid" )), (unsigned int)pretrigger_samples );
			handleErr( DAQmxCfgDigEdgeRefTrig (taskHandle, "/"DEV_DEV"/"DEV_TRIGGER_INPUT, trigger_edge, pretrigger_samples) );
		}
	}else{
		deprintf ("No external trigger has been set up. Sampling will begin immediately at DAQmxStartTask()...\n")
		if (pretrigger_samples){
			teprintf("Deleting Trigger-Ready signal file before fatal error: fake the trigger to prevent hang.\n");  /* Fake trigger, prevent hang. As above */
			feprintf ("Fatal error: can't have pre-trigger samples with internal triggering. (Use -t 'fe/re' with '-p').\n");   //can we?
		}
	}

	/* Configure Timing and Sample Count. Use Rising Edge of Onboard clock (arbitrary choice, makes very little difference). Function call takes ~ 0.2 ms. */
	/* Documented at /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxcfgsampclktiming.html */
	if (continuous){
		/* Acquire samples continuously at a rate of (coereced)sample_rate. */
		deprintf ("DAQmxCfgSampClkTiming: acquiring continuous samples (in buffer size %ld), at %f Hz, using the %s edge of the internal sample-clock...\n", (long)(sizeof(data)/sizeof(data[0])), sample_rate, edge_arg);
		handleErr( DAQmxCfgSampClkTiming(taskHandle, OnboardClock, sample_rate, clock_edge, DAQmx_Val_ContSamps, (sizeof(data)/sizeof(data[0])) ) );   /* Continuous samples. final parameter is re-purposed, though the value seems unimportant. */
		handleErr( DAQmxGetSampClkRate (taskHandle, &readback_hz)  ); 	/* Check coercion */
		deprintf ("Sample clock requested: %f Hz; actually coerced by device to: %f Hz.\n", sample_rate, readback_hz);
	}else{
		/* Acquire finite number, num_samples, of samples (on each channel) at a rate of (coereced)sample_rate. Min value is 2. */
		deprintf ("DAQmxCfgSampClkTiming: acquiring (finite) %lld samples, at %f Hz, using the %s edge of the internal sample-clock...\n", (long long)(num_samples + adcdelay_discard_samples), sample_rate, edge_arg);
		handleErr( DAQmxCfgSampClkTiming(taskHandle, OnboardClock, sample_rate, clock_edge, DAQmx_Val_FiniteSamps, (num_samples + adcdelay_discard_samples) ) );  /* Finite number of samples (include the ones to discard) */
		handleErr( DAQmxGetSampClkRate (taskHandle, &readback_hz)  ); 	/* Check coercion */
		deprintf ("Sample clock requested: %f Hz; actually coerced by device to: %f Hz.\n", sample_rate, readback_hz);

		/* Ensure that DAQmxReadAnalogF64() (below) will not block for completion, but will read all the available samples. Function call takes ~ 0.03 ms. */
		/* NB iff this is set to FALSE, then Nonblocking reads that return 0 reads will return a timeout error; otherwise ok */
		deprintf ("DAQmxSetReadReadAllAvailSamp: setting to true, to enable DAQmxReadAnalogF64() with DAQmx_Val_Auto to be non-blocking even when num_samples is finite...\n");
		handleErr( DAQmxSetReadReadAllAvailSamp (taskHandle, TRUE) );
	}

	/* Get the size of the onboard and input buffer. Just for interest, atm. */
	/* Documented at:  /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func230a.html , /usr/local/natinst/nidaqmx/docs/mxcprop.chm/func186c.html */
	deprintf ( "DAQmxGetBufInputOnbrdBufSize: getting size of the onboard buffer...\n");	/* Physical hardware */
	handleErr( DAQmxGetBufInputOnbrdBufSize(taskHandle, &readback_onboard_buf_size) );
	deprintf ( "DAQmxGetBufInputBufSize: getting size of the input buffer...\n");		/* Main RAM. In finite sampling mode, automatically assigned (but can be overridden). In continuous mode, assigned by DAQmxCfgSampClkTiming */
	handleErr( DAQmxGetBufInputBufSize(taskHandle, &readback_input_buf_size) );
	deprintf ( "The onboard buffer size is %u samples per channel. The input buffer is %u samples per channel.\n", (unsigned int)readback_onboard_buf_size, (unsigned int)readback_input_buf_size );


	/* Set EnhancedAliasRejectionEnable as #defined above. (It normally defaults to on). */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/attr2294.html */
	deprintf ("DAQmxSetAIEnhancedAliasRejectionEnable: setting to %s.\n",  enable_adc_lf_ear_arg );
	handleErr( DAQmxSetAIEnhancedAliasRejectionEnable(taskHandle, input_channel, enable_adc_lf_ear) );
	handleErr( DAQmxGetAIEnhancedAliasRejectionEnable(taskHandle, input_channel, &enh_alias_reject) );
	deprintf ("EnhancedAliasRejectionEnable: readback %d.\n", (int)enh_alias_reject );


	/* Write out header to file (use the readback values where they might differ from the requested ones). */
	outprintf ("#Data from %s (%s):\n", DEV_NAME, DEV_DEV);
	outprintf ("#timestamp: %ld\n", then.tv_sec); 
	outprintf ("#freq_hz:  %.3f\n", readback_hz);	outprintf ("#samples:  %s\n", samplenum_arg); 	outprintf ("#pretrigger_samples: %d\n", (unsigned int)pretrigger_samples);
	outprintf ("#channel:  %s\n", channel_arg);	outprintf ("#voltage:  %.3f\n", readback_v2);	outprintf ("#gain:     %.1f\n", readback_g);
	outprintf ("#coupling: %s\n", coupling_arg); 	outprintf ("#terminal: %s\n", terminal_arg);	outprintf ("#trigger:  %s\n", triggering_arg);
	outprintf ("#clk_edge: %s\n", edge_arg); 	outprintf ("#format:   %s\n", format_arg);	outprintf ("#lf_ear:   %s\n", enable_adc_lf_ear_arg);
	outprintf ("#initial_discard: %d\n", adcdelay_discard_samples);  


	/* Commit the task (make sure all hardware is configured and ready). [This is also implicit in StartTask() if it hasn't been done]. Useful if we want to repeat the sampling task in a loop. Function call takes ~ 390 ms. */
	/* WARNING. If Start/Stop task will be in a loop and Sample_Freq < 2kHz, avoid taskCommit: it has side-effects that will cause DAQmxReadAnalogF64() to fail at random. Rely on the implicitness in StartTask().  */
	/* See also: ni4462_bug_dont_use_task_commit.c  and http://forums.ni.com/t5/Multifunction-DAQ/Bug-DAQmxReadAnalogF64-DAQmx-Val-Auto-DAQmx-Val-WaitInfinitely/m-p/1933361 */
	deprintf ("DAQmxTaskControl: committing task (%d)\n", DAQmx_Val_Task_Commit);
	state = "Committing";
	handleErr( DAQmxTaskControl ( taskHandle, DAQmx_Val_Task_Commit) );  /* Error: DAQmxErrorPALResourceReserved  i.e. -50103  arises if there are two processes contending for access to this device. */

	if (need_to_settle_dcac == 1){  /* Do this here, not above. Otherwise we are putting in the delay before switching the switches! */
		deprintf ("Device was initialised with DC-coupling. Changed to AC-coupling; sleeping %.3f seconds to let it settle...\n", (2* DEV_DCAC_SETTLETIME_S));
		usleep  ( (int)(DEV_DCAC_SETTLETIME_S * 2e6) ); /* Double the time, to allow for impedance of source as well as NI4462 */
		deprintf("    ...done.\n");
	}
	if (need_to_settle_newgain == 1){
		deprintf ("User says (-g) that the gain is different this time; sleeping %.3f seconds to ensure the pre-amp has settled...\n", DEV_PREAMP_NEWGAIN_SETTLETIME_S);
		usleep  ( (int)(DEV_PREAMP_NEWGAIN_SETTLETIME_S * 1e6) ); /* Double the time, to allow for impedance of source as well as NI4462 */
		deprintf("    ...done.\n");
	}
	state = "Committed";
	gettimeofday(&now, NULL);
	deprintf("Setup time (for CreateTask...CommitTask) was: %.3g s.\n", (now.tv_sec - then.tv_sec + 1e-6 * (now.tv_usec - then.tv_usec)) );


	/* Start the task. Sampling starts now, unless we defined an external trigger (with DAQmxCfgDigEdgeStartTrig() above). Function call takes ~ 1.6 ms (if already committed). */
        /* At this point, IFF we are in Reference trigger mode and IF PFI0 is not already held in the active state, RTSI6 will start to be clocked */
	/* WARNING: it can take up to 2 seconds to get here (occasionally more); triggers received before now will be ignored. Hence the implementation of -T. */
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxstarttask.html */
	deprintf ("DAQmxStartTask: Starting task...\n");
	handleErr( DAQmxStartTask(taskHandle) );
	if (trigger_ext){		/* Make it explicit, especially if we have just received a trigger and failed to respond to it because we were not ready! */
		eprintf ("NI4462 waiting for trigger.\n");
		state = "Ready/Running"; /* Best we can do: can't distinguish "waiting for trigger" from "triggered and sampling". */
	}else{
		state = "Running";
	}
	if (do_triggerready_delete){	/* Tell an external process that we are ready for our trigger.  Normally useful with ext-trigger. */
 		deprintf ("Deleting Trigger-Ready signal-file '%s'.\n", triggerready_filename);
		unlink (triggerready_filename);
	}
	
	
	/* Discard initial X samples, to account for ADC filter delay and avoid pre-capturing. Do a blocking read here. */
	/* The right way would be to use DAQmxSetDelayFromSampClkDelay () and DAQmxSetDelayFromSampClkDelayUnits(), but these are not available on this device. */
	/* Alternatively, and better, actually delay the trigger signal before sending it to the NI4462 (see arduino_delay) */
	/* Discarding samples is ugly, can cause problems if we are in a tight loop and wanted them for the previous run, and the timing wrong (must round to integer number of samples!) */
	if (adcdelay_discard_samples > 0){
		n = adcdelay_discard_samples;  j = 0;
		deprintf ("Discarding the first %d samples as junk...\n", adcdelay_discard_samples);
		outprintf("#preserving the initial discarded samples (invoked with '-j %d'); at most %d will be kept:\n", adcdelay_discard_samples, MAX_COMMENTED_DISCARDED_SAMPS);
		while (n > 0){
			m =  (n > BUFFER_SIZE_TUPLES) ? BUFFER_SIZE_TUPLES : n ;	/* discard in chunks of m; n may be too large for the buffer. */
			deprintf ("DAQmxReadAnalogF64: Blocking read of %d samples (out of %d) to be discarded as junk...\n", m, adcdelay_discard_samples);
			handleErr( DAQmxReadAnalogF64(taskHandle, m, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &num_samples_read_thistime, NULL) );
			deprintf ("    ...acquired %d points, discarding the data.\n",(int)num_samples_read_thistime);
			n -= num_samples_read_thistime;
			for (i=0; i < num_samples_read_thistime; i++, j++){  /* include the junk data as comments, just in case */
				if (j > MAX_COMMENTED_DISCARDED_SAMPS){	     /* (skip after the first 1k, or file can get very large) */
					break;
				}else if (num_channels == 1){
					outprintf("#%f\n",data[i]);
				}else if (sum_channels == 1){
					outprintf("#%f\n",(data[DEV_NUM_CH*i] + data[DEV_NUM_CH*i+1] + data[DEV_NUM_CH*i+2] + data[DEV_NUM_CH*i+3]) );
				}else{
					outprintf("#%f\t%f\t%f\t%f\n", data[DEV_NUM_CH*i], data[DEV_NUM_CH*i+1], data[DEV_NUM_CH*i+2], data[DEV_NUM_CH*i+3]);
				}
			}
		}
	}	

	//Set handler for Ctrl-C. Within the following while loop only, Ctrl-C must break out of the loop, not kill the program */
	signal(SIGINT, handle_signal_cc);

	/* Loop, reading data and writing it to file. We might be reading continuously, or might have a finite number of samples which is too large for any buffer. */
	/* We WANT to do select(), i.e. block until there is at least 1 sample available, then read as much as there is. One might expect that using:
	 *    DAQmxReadAnalogF64(DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely) would achieve this (documentation is unclear), but in fact, it returns immediately if there are no reads.
	 * So, first do a blocking read of exactly 1 data point, then do a non-blocking read of as many samples are available. Rather ugly, but it does work!
	 * Actually, slightly more efficient: do a non-blocking read, then (iff we got zero samples), do the blocking read. */
	while (1){

		/* First, let the device actually do some sampling! We'll block if there is nothing to read. */

		if (format_floatv){   /* Read data in floatV format (default) */

			/* Non-blocking read of all the samples that are available. May return 0 or more samples (automatically safely limited by size of buffer). */
			/* Note that DAQmx_Val_Auto works the same way for both finite and continuous sampling, thanks to DAQmxSetReadReadAllAvailSamp(TRUE) above */
			/* Given the other settings, the 3rd parameter of DAQmxReadAnalogF64() can be either "0" or DAQmx_Val_WaitInfinitely. The effect is identical [except in Reference-trigger mode, where the latter is fine, but 0 causes error -200281.] */
			/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxreadanalogf64.html */
			vdeprintf("DAQmxReadAnalogF64: Non-blocking read of as many samples as available, immediate timeout...\n");
			handleErr( DAQmxReadAnalogF64(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &num_samples_read_thistime, NULL) );
			num_samples_read_total += num_samples_read_thistime;
			vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)num_samples_read_thistime, (long long)num_samples_read_total);

			/* IFF we got zero samples, now do a blocking read, and wait. Otherwise, optimise: guess that it won't block, so wait for the next iteration. */
			/* NB, because we got ZERO samples last time, we can start writing at data, not data+n. */
			if (num_samples_read_thistime == 0){
				/* Blocking read of 1 sample. */
				vdeprintf("DAQmxReadAnalogF64: Blocking read of 1 sample, infinite timeout...\n");
				/* This is where we block, if waiting for an external trigger input */
				if ( (num_samples_read_total == 0) && (trigger_ext == 1) ){
					deprintf ("Waiting for external trigger (blocking read)...\n");
				}
				handleErr( DAQmxReadAnalogF64(taskHandle, 1, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data, (sizeof(data)/sizeof(data[0])), &num_samples_read_thistime, NULL) );
				num_samples_read_total += num_samples_read_thistime;
				vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)num_samples_read_thistime, (long long)num_samples_read_total);
				if (num_samples_read_thistime != 1){  /* sanity check. */
					feprintf ("This shouldn't be possible: blocking read of 1 sample with infinite timeout returned %d samples, expected 1.\n", (int)num_samples_read_thistime);
				}
			}

			/* Now write out the data to file in the right format. At the same time, add up sum[],sum_squares[] for the stats. */
			for (i=0; i < num_samples_read_thistime; i++){
				if ((num_samples && continuous) && (i >= abs(num_samples + num_samples_read_thistime - num_samples_read_total))){ /* Special case of large, finite number of samples, promoted to "cont", on the final read: discard any surplus data. */
					deprintf ("Large, finite samples in continuous mode; discarding %d surplus samples from end.\n", (int)num_samples_read_thistime - i);
					num_samples_read_total = num_samples;
					break;
				}
				if (num_channels == 1){		/* 1 channel only */
					output_1f ( data[i] );
					sum[0] += data[i];
					sum_squares[0] += data[i]*data[i];
				}else if (sum_channels == 1){  /*  4 channels, summed */
					data_tmp = data[DEV_NUM_CH*i] + data[DEV_NUM_CH*i+1] + data[DEV_NUM_CH*i+2] + data[DEV_NUM_CH*i+3];
					output_1f (data_tmp);
					sum[0] += data_tmp;
					sum_squares[0] += data_tmp * data_tmp;
				}else{				/* 4 channnels, separate */
					output_4f ( data[DEV_NUM_CH*i], data[DEV_NUM_CH*i+1], data[DEV_NUM_CH*i+2], data[DEV_NUM_CH*i+3] );
					sum[0] += data[DEV_NUM_CH*i]; sum[1] += data[DEV_NUM_CH*i+1]; sum[2] += data[DEV_NUM_CH*i+2]; sum[3] += data[DEV_NUM_CH*i+3];
					sum_squares[0] += data[DEV_NUM_CH*i] * data[DEV_NUM_CH*i]; sum_squares[1] += data[DEV_NUM_CH*i+1] * data[DEV_NUM_CH*i+1]; sum_squares[2] += data[DEV_NUM_CH*i+2] * data[DEV_NUM_CH*i+2]; sum_squares[3] += data[DEV_NUM_CH*i+3] * data[DEV_NUM_CH*i+3];
				}
			}

			/* Print some sample data points for debugging: the first 10. */
			for (i=0; ( (i < num_samples_read_thistime) && (num_samples_printed < 10) ); i++, num_samples_printed++){
				if (num_channels == 1){		/* 1 channel only */
					deprintf("Data value %d is: %f\n",num_samples_printed,data[i]);
				}else if (sum_channels == 1){  /*  4 channels, summed */
					deprintf("Data value %d is: %f\n",num_samples_printed,  (data[DEV_NUM_CH*i] + data[DEV_NUM_CH*i+1] + data[DEV_NUM_CH*i+2] + data[DEV_NUM_CH*i+3]) );
				}else{				/* 4 channnels, separate */
					deprintf("Data value %d is: %f, %f, %f, %f\n",num_samples_printed, data[DEV_NUM_CH*i], data[DEV_NUM_CH*i+1], data[DEV_NUM_CH*i+2], data[DEV_NUM_CH*i+3]);
				}
			}

		}else{	/* Read data in int32ADC format. Otherwise, as above. Rather ugly to duplicate so much code, just to change type of data vs data_i. We could do it with function pointers, but that would make it less readable. */

			/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxreadbinaryi32.html */
			vdeprintf  ("DAQmxReadBinaryI32: Non-blocking integer read of as many samples as available, immediate timeout...\n");
			handleErr( DAQmxReadBinaryI32(taskHandle, DAQmx_Val_Auto, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data_i, (sizeof(data_i)/sizeof(data_i[0])), &num_samples_read_thistime, NULL) );
			num_samples_read_total += num_samples_read_thistime;
			vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)num_samples_read_thistime, (long long)num_samples_read_total);

			if (num_samples_read_thistime == 0){
				vdeprintf  ("DAQmxReadBinaryI32: Blocking integer read of 1 sample, infinite timeout...\n");
				/* This is where we block, if waiting for an external trigger input */
				if ( (num_samples_read_total == 0) && (trigger_ext == 1) ){
					deprintf ("Waiting for external trigger (blocking read)...\n");
				}
				handleErr( DAQmxReadBinaryI32(taskHandle, 1, DAQmx_Val_WaitInfinitely, DAQmx_Val_GroupByScanNumber, data_i, (sizeof(data_i)/sizeof(data_i[0])), &num_samples_read_thistime, NULL) );
				num_samples_read_total += num_samples_read_thistime;
				vdeprintf("    ...acquired %d points this time; total is: %lld.\n",(int)num_samples_read_thistime, (long long)num_samples_read_total);
				if (num_samples_read_thistime != 1){  /* sanity check. */
					feprintf ("Fatal Error: blocking read of 1 sample with infinite timeout returned %d samples, expected 1.\n", (int)num_samples_read_thistime);
				}
			}

			for (i=0; i < num_samples_read_thistime; i++){
				if ((num_samples && continuous) && (i >= abs(num_samples + num_samples_read_thistime - num_samples_read_total))){ /* Special case of large, finite number of samples, promoted to "cont", on the final read: discard any surplus data. */
					deprintf ("Large, finite samples in continuous mode; discarding %d surplus samples from end.\n", (int)num_samples_read_thistime - i);
					num_samples_read_total = num_samples;
					break;
				}
				if (num_channels == 1){
					output_1d ( (int)data_i[i] );
					sum[0] += data_i[i];
					sum_squares[0] += (float64)data_i[i] * data_i[i];
				}else if (sum_channels == 1){
					data_i_tmp = data_i[DEV_NUM_CH*i] + data_i[DEV_NUM_CH*i+1] + data_i[DEV_NUM_CH*i+2] + data_i[DEV_NUM_CH*i+3];
					output_1d ((int)data_i_tmp);
					sum[0] += data_i_tmp;
					sum_squares[0] += (float64)data_i_tmp * data_i_tmp;
				}else{
					output_4d ( (int)data_i[DEV_NUM_CH*i], (int)data_i[DEV_NUM_CH*i+1], (int)data_i[DEV_NUM_CH*i+2], (int)data_i[DEV_NUM_CH*i+3] );
					sum[0] += data_i[DEV_NUM_CH*i]; sum[1] += data_i[DEV_NUM_CH*i+1]; sum[2] += data_i[DEV_NUM_CH*i+2]; sum[3] += data_i[DEV_NUM_CH*i+3];
					sum_squares[0] += (float64)data_i[DEV_NUM_CH*i] * data_i[DEV_NUM_CH*i]; sum_squares[1] += (float64)data_i[DEV_NUM_CH*i+1] * data_i[DEV_NUM_CH*i+1]; sum_squares[2] += (float64)data_i[DEV_NUM_CH*i+2] * data_i[DEV_NUM_CH*i+2]; sum_squares[3] += (float64)data_i[DEV_NUM_CH*i+3] * data_i[DEV_NUM_CH*i+3];
				}
			}

			for (i=0; ( (i < num_samples_read_thistime) && (num_samples_printed < 10) ); i++, num_samples_printed++){
				if (num_channels == 1){		/* 1 channel only */
					deprintf("Data value %d is: %d\n",num_samples_printed,(int)data_i[i]);
				}else if (sum_channels == 1){  /*  4 channels, summed */
					deprintf("Data value %d is: %d\n",num_samples_printed,  ((int)data_i[DEV_NUM_CH*i] + (int)data_i[DEV_NUM_CH*i+1] + (int)data_i[DEV_NUM_CH*i+2] + (int)data_i[DEV_NUM_CH*i+3]) );
				}else{				/* 4 channnels, separate */
					deprintf("Data value %d is: %d, %d, %d, %d\n",num_samples_printed, (int)data_i[DEV_NUM_CH*i], (int)data_i[DEV_NUM_CH*i+1], (int)data_i[DEV_NUM_CH*i+2], (int)data_i[DEV_NUM_CH*i+3]);
				}
			}
		}


		/* Flush data to file (useful if we are waiting for slooow sampling) */
		fflush (outfile);

		/* Have we now got all the samples we need? */
		if ( ( (!continuous) || (continuous && (num_samples != 0)) ) && (num_samples_read_total >= num_samples) ){  	/* '>=' is for safety; '==' is correct. */
			deprintf ("Finished acquiring all %lld samples...breaking out of loop.\n", (long long)num_samples);	/* '(continuous && (num_samples != 0))' is for large N, promoted to cont. */
			break;
		}

		/* Have we just received a Ctrl-C ? */
		if ( terminate_loop){  /* global */
			deprintf ("Terminating this loop early.\n");
			num_samples = num_samples_read_total;  /* set num_samples to what we actually got, not what we wanted. (also important for continuous mode) */
			break;
		}
	}

	/* Reset signal handler to default ? Maybe better to leave it. */
	// signal(SIGINT, SIG_DFL);


	/* Check whether an overload (either digital or analog) occurred during this task. Must do this before stopping the task. */
	/* Documented at /usr/local/natinst/nidaqmx/docs/mxcprop.chm/attr2174.html */
	deprintf ("DAQmxGetReadOverloadedChansExist: checking whether an overload occurred...\n");
	handleErr( DAQmxGetReadOverloadedChansExist (taskHandle, &overload_occurred) );   /* NB Performing this check also clears the overload 'flag' */
	if (overload_occurred){
		/* Get details of which channel it was. */
		/* Documented at: /usr/local/natinst/nidaqmx/docs/mxcprop.chm/attr2175.html */
		deprintf ("    ...Yes!  Now, DAQmxGetReadOverloadedChans: finding the details...\n");
		handleErr( DAQmxGetReadOverloadedChans(taskHandle, error_buf, sizeof(error_buf) ) );  /* NB must only do this after DAQmxGetReadOverloadedChansExist() said yes. */
		feprintf ("Fatal Error: an overload has occurred, in channel(s): '%s'. Beware preamp saturation transient; use -g next time.\n", error_buf);
	}else{
		deprintf ("    ...OK, no overload has happened.\n")
	}


	/* Stop the task. Function call takes: ~ 0.8 ms.*/
	/* Documented at: /usr/local/natinst/nidaqmx/docs/daqmxcfunc.chm/daqmxstoptask.html   and daqmxcleartask.html  */
	deprintf ("DAQmxStopTask, DAQmxClearTask: stopping and clearing task...\n");
	handleErr( DAQmxStopTask(taskHandle) );	 /* After stopping, we could start() again, without needing to repeat the configuration. */
	handleErr( DAQmxClearTask(taskHandle) ); /* Clearing the task discards its configuration. [even if we omit these calls, they are is implicit when this program exits. */
	state = "Stopped";

	/* Calculate and print statistics */
	if (format_floatv){  /* units and multipliers */
		mv = "mV"; uv = "uV"; mvx = 1000; uvx = 1e6;
	}else{
		mv = "bits";    uv = "bits";  mvx = 1;    uvx = 1;
	}
	for (i=0 ; i < DEV_NUM_CH; i++){
		mean[i] = sum[i] / num_samples;   /* note: if there is only a single channel used, look in sum[0], not sum[channel_num] etc */
		var[i] =  (sum_squares[i] / num_samples) - (mean[i] * mean[i]);
		stddev[i] = sqrt(fabs(var[i]));   /* fabs() protects from error if tiny number becomes tiny negative */
	}
	if (do_stats && ! debug){ /* Short summary */
		eprintf ("Measured %lld samples on channel %s at %.4f Hz.  Voltage: +/- %.3f V. Gain: %.1f. Coupling: %s. Terminal_mode: %s. Initial_junk_samples: %d.\n", (long long)num_samples, channel_arg, readback_hz, readback_v2, readback_g, coupling_arg, terminal_arg, adcdelay_discard_samples);
	}
	if (do_stats || debug){
		if (num_channels == 1){		/* 1 channel only */
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Channel: %s.)\n", mean[0]*mvx, mv, stddev[0]*uvx, uv, (long long)num_samples, channel_arg);	//Convert to mV and uV.
		}else if (sum_channels == 1){  /*  4 channels, summed */
			mean_s = ( mean[0] + mean[1] + mean[2] + mean[3] );
			stddev_s = ( stddev[0] + stddev[1] + stddev[2] + stddev[3] );  /* Assume these are NOT INDEPENDENT, so add the stddevs, not the variances. */
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Sum of %d non-independent channels.)\n", mean_s*mvx, mv, stddev_s*uvx, uv, (long long)num_samples, DEV_NUM_CH);
		}else{				/* 4 channnels, separate */
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Channel: 0.)\n", mean[0]*mvx, mv, stddev[0]*uvx, uv, (long long)num_samples);
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Channel: 1.)\n", mean[1]*mvx, mv, stddev[1]*uvx, uv, (long long)num_samples);
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Channel: 2.)\n", mean[2]*mvx, mv, stddev[2]*uvx, uv, (long long)num_samples);
			eprintf ("Mean is %8.4f %s,  stddev is %10.4f %s,  num is %lld samples. (Channel: 3.)\n", mean[3]*mvx, mv, stddev[3]*uvx, uv, (long long)num_samples);
		}
	}

	/* Brief summary on one line. This is intended to be very short, for speech synthesis. Eg pipe through festival, looping, while probing various signal sources. */
	/* Means first, then Std-Devs. (so that the std-dev is always the last line, used by other programs which may `tail -n1` this output.) */
	if (do_brief_mean){
		if (num_channels == 1){		/* 1 channel only */
			eprintf ("%8.4f\t\t#mean (mV), input %s\n", mean[0]*mvx, channel_arg);
		}else if (sum_channels == 1){  /*  4 channels, summed */
			mean_s = ( mean[0] + mean[1] + mean[2] + mean[3] );
			eprintf ("%8.4f\t\t#mean (mV), sum\n", mean_s *mvx);
		}else{				/* 4 channnels, separate */
			eprintf ("%8.4f\t%8.4f\t%8.4f\t%8.4f\t\t#mean (mV), inputs 0,1,2,3\n", mean[0]*mvx, mean[1]*mvx, mean[2]*mvx, mean[3]*mvx);
		}
	}
	if (do_brief_sd){
		if (num_channels == 1){		/* 1 channel only */
			eprintf ("%8.2f\t\t#std-dev (uV), input %s\n", stddev[0]*uvx, channel_arg);
		}else if (sum_channels == 1){  /*  4 channels, summed */
			stddev_s = ( stddev[0] + stddev[1] + stddev[2] + stddev[3] );  /* Assume these are NOT INDEPENDENT, so add the stddevs, not the variances. */
			eprintf ("%8.2f\t\t#std-dev (uV), sum\n", stddev_s *uvx);
		}else{				/* 4 channnels, separate */
			eprintf ("%8.2f\t%8.2f\t%8.2f\t%8.2f\t\t#std-dev (uV), inputs 0,1,2,3\n", stddev[0]*uvx, stddev[1]*uvx, stddev[2]*uvx, stddev[3]*uvx);
		}
	}

	/* Done! */
	deprintf ("Cleaning up after libnidaqmx: removing lockfiles from NI tempdir, %s .\n", LIBDAQMX_TMPDIR)   /* libdaqmx should clean up its own lockfiles, but doesn't. */
	ret = system ( "rm -f "LIBDAQMX_TMPDIR"ni_dsc_osdep_*" );   /* Risky. Part of the path is hardcoded here, as a slight safety measure. */
	if (ret != 0){
		deprintf ("Problem cleaning up.\n")
	}
        fclose ( outfile );
	if (do_syslog){
		closelog();
	}
	if (survived_count > 0){
		eprintf ("Warning: survived %d errors that should have been fatal (-A).\n", survived_count); /* If we survived fatal errors, now is the time to complain. */
		return (EXIT_FAILURE);
	}else{
		return (EXIT_SUCCESS);
	}
}
