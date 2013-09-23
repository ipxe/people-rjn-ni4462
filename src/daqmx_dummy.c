/* This is a dummy file whose job is to allow ni4462_test.c to be compiled successfully on systems (notably 64-bit ones) where libnidaqmx isn't available. 
   It doesn't "do" anything useful. Copyright Richard Neill <ni4462 at richardneill dot org>, 2011-2012. Free Software released under the GNU GPL v3+. 
   Note that most of the functions merely print a dummy message, and return success. Returned data should be validly formatted, but it's garbage.
*/

/* Constants used by DAQmx */
#define FALSE				0
#define TRUE				1
#define DAQmx_Val_Auto			-1	/* <-- this value is important */
#define DAQmx_Val_AC			777	/* these values are made up... */
#define DAQmx_Val_ContSamps		778
#define DAQmx_Val_DC			779
#define DAQmx_Val_Diff			780
#define DAQmx_Val_DoNotInvertPolarity	781
#define DAQmx_Val_Falling		782
#define DAQmx_Val_FiniteSamps		783
#define DAQmx_Val_GND			784
#define DAQmx_Val_GroupByScanNumber	785
#define DAQmx_Val_InvertPolarity	786
#define DAQmx_Val_NRSE			787
#define DAQmx_Val_PseudoDiff		788
#define DAQmx_Val_Rising		789
#define DAQmx_Val_RSE			790
#define DAQmx_Val_Task_Commit		791
#define DAQmx_Val_Volts			792
#define DAQmx_Val_WaitInfinitely	793

/* Task Handle. DAQmx passes around something which presumably refers to some internal struct. As we will always "succeed" and always return dummy data, this can be a simple int here.  */
typedef int 		TaskHandle;

/* Other typedefs used by DAQmx */
typedef int32_t 	int32;
typedef uint32_t	uInt32;
typedef int64_t		int64;
typedef uint64_t	uInt64;
typedef int		bool32;
typedef double		float64;

/* Globals */
bool32	will_read_all_available = FALSE;
uInt64	samples_remaining_in_task = 0;
float64 settings_voltage_min = -10;
float64 settings_voltage_max = 10;
float64 settings_gain = 100;
int32 	settings_mode = DAQmx_Val_Diff;
int32 	settings_coupling = DAQmx_Val_DC;
int32 	settings_edge = DAQmx_Val_Falling; 
int32 	settings_lfear = TRUE;
float64 settings_rate = 200000;
int	settings_num_channels = 1;


/* Get error message. */
int DAQmxGetErrorString(int error, char *error_buf, int size){
	error_buf = "Dummy error message";
	fprintf (stderr, "Dummy DAQmxGetErrorString (%d, %s, %d).\n", error, error_buf, size);
	return (0);
}
/* Get extended error message */
int DAQmxGetExtendedErrorInfo(char *error_buf, int size){
	error_buf = "Dummy error message (long version)";
	fprintf (stderr, "Dummy DAQmxGetExtendedErrorInfo (%s, %d).\n", error_buf, size);
	return (0);
}
/* Did it fail? */
int DAQmxFailed(int error){
	return (error < 0);
}


/* Task control */
int DAQmxCreateTask( char *name, TaskHandle *taskHandle){
	fprintf (stderr, "Dummy DAQmxCreateTask (%s, %d).\n", name, *taskHandle);
	return (0);
}
int  DAQmxStartTask(TaskHandle taskHandle){
	fprintf (stderr, "Dummy DAQmxStartTask (%d).\n", taskHandle);
	return (0);
}
int  DAQmxStopTask(TaskHandle taskHandle){
	fprintf (stderr, "Dummy DAQmxStopTask (%d).\n", taskHandle);
	return (0);
}	
int  DAQmxClearTask(TaskHandle taskHandle){
	fprintf (stderr, "Dummy DAQmxClearTask (%d).\n", taskHandle);
	return (0);
}
int DAQmxTaskControl(TaskHandle taskHandle, int32 control){
	fprintf (stderr, "Dummy DAQmxTaskControl (%d, %d).\n", taskHandle, control);
	return (0);
}


/* Get information about the system */
int DAQmxGetDevProductNum(char *device, uInt32 *data){
	fprintf (stderr, "Dummy DAQmxGetDevProductNum (%s).\n", device);
	*data = 0x11223344;
	return (0);
}
int DAQmxGetDevSerialNum(char *device, uInt32 *data){
	fprintf (stderr, "Dummy DAQmxGetDevSerialNum (%s).\n", device);
	*data = 0x12345678;
	return (0);
}
int DAQmxGetSysNIDAQMajorVersion(uInt32 *data){
	fprintf (stderr, "Dummy DAQmxGetSysNIDAQMajorVersion.\n");
	*data = 0xff;
	return (0);
}
int DAQmxGetSysNIDAQMinorVersion(uInt32 *data){
	fprintf (stderr, "Dummy DAQmxGetSysNIDAQMinorVersion.\n");
	*data = 0xee;
	return (0);
}
int DAQmxGetExtCalLastDateAndTime (char *device, uInt32 *year, uInt32 *month, uInt32 *day, uInt32 *hour, uInt32 *minute){
	fprintf (stderr, "Dummy DAQmxGetExtCalLastDateAndTime (%s).\n", device);
	*year=2012; *month=01; *day=02; *hour=03; *minute=04;
	return (0);
}
int DAQmxGetSelfCalLastDateAndTime (char *device, uInt32 *year, uInt32 *month, uInt32 *day, uInt32 *hour, uInt32 *minute){
	fprintf (stderr, "Dummy DAQmxGetSelfCalLastDateAndTime (%s).\n", device);
	*year=2013; *month=02; *day=03; *hour=04; *minute=05;
	return (0);
}

/* Self Calibrate. */
int DAQmxSelfCal (char *device){
	fprintf (stderr, "Dummy DAQmxSelfCal (%s).\n", device);
	return (0);
}

/* Reset Device. */
int DAQmxResetDevice (char *device){
	fprintf (stderr, "Dummy DAQmxResetDevice (%s).\n", device);
	return (0);
}

/* Connect terminals */
int  DAQmxConnectTerms (char *source, char *dest, int modifiers){
	fprintf (stderr, "Dummy DAQmxConnectTerms (%s, %s, %d).\n", source, dest, modifiers);
	return(0);
}

/* Create AI voltage channel */
int DAQmxCreateAIVoltageChan (TaskHandle taskHandle, char *physicalchannel, char *name, int config, float64 minval, float64 maxval, int units, char* scalename){
	fprintf (stderr, "Dummy DAQmxCreateAIVoltageChan (%d, %s, %s, %d, %f, %f, %d, %s).\n",  taskHandle, physicalchannel, name, config, minval, maxval, units, scalename);
	settings_voltage_min = minval;  /* globals */	
	settings_voltage_max = maxval;
	if (strlen(basename(physicalchannel)) == 3){  /* Eg "ai0" */
		settings_num_channels = 1;
	}else if (strlen(basename(physicalchannel)) == 6){
		fprintf (stderr, "Error: can't count channels in: '%s'\n", basename(physicalchannel));
		exit (1);
	}else{
		char channels[16];	 /* Eg "ai0:3". Ugly. */
		strncpy (channels, basename(physicalchannel), 16);
		channels[3] = 0;
		channels[5] = 0;
		settings_num_channels = atoi(channels+4) - atoi(channels+2) + 1;
	}
	return (0);
}

/* Get/Set input configuration */
int DAQmxGetAIMin (TaskHandle taskHandle, char *physicalchannel, float64 *readback_v1){
	fprintf (stderr, "Dummy DAQmxGetAIMin (%d, %s).\n", taskHandle, physicalchannel); 
	*readback_v1 = settings_voltage_min; /* global */	
	return(0);
}
int DAQmxGetAIMax (TaskHandle taskHandle, char *physicalchannel, float64 *readback_v2){
	fprintf (stderr, "Dummy DAQmxGetAIMax (%d, %s).\n", taskHandle, physicalchannel); 
	*readback_v2 = settings_voltage_max; /* global */	
	return(0);
}	
int DAQmxGetAIGain (TaskHandle taskHandle, char *physicalchannel, float64 *gain){
	fprintf (stderr, "Dummy DAQmxGetAIGain (%d, %s).\n", taskHandle, physicalchannel); 
	*gain = settings_gain; /* global */
	return(0);
}	
int DAQmxGetAITermCfg (TaskHandle taskHandle, char *physicalchannel, int32 *mode){
	fprintf (stderr, "Dummy DAQmxGetAITermCfg (%d, %s).\n", taskHandle, physicalchannel); 
	*mode = settings_mode; /* global */
	return(0);
}	
int DAQmxGetAICoupling (TaskHandle taskHandle, char *physicalchannel, int32 *coupling){
	fprintf (stderr, "Dummy DAQmxGetAICoupling (%d, %s).\n", taskHandle, physicalchannel); 
	*coupling = settings_coupling; /* global */
	return(0);
}	
int DAQmxSetAICoupling (TaskHandle taskHandle, char *physicalchannel, int32 coupling){
	fprintf (stderr, "Dummy DAQmxGetAICoupling (%d, %s, %d).\n", taskHandle, physicalchannel, coupling); 
	settings_coupling = coupling; /* global */
	return(0);
}	
int DAQmxCfgDigEdgeStartTrig (TaskHandle taskHandle, char *input, int32 edge){
	fprintf (stderr, "Dummy DAQmxCfgDigEdgeStartTrig (%d, %s, %d).\n", taskHandle, input, edge); 
	settings_edge = edge; /* global */
	return(0);
}	
int DAQmxCfgDigEdgeRefTrig (TaskHandle taskHandle, char *input, int32 edge, int pretrigger_samples){
	fprintf (stderr, "Dummy DAQmxCfgDigEdgeRefTrig (%d, %s, %d, %d).\n", taskHandle, input, edge, pretrigger_samples); 
	settings_edge = edge; /* global */
	return(0);
}
int DAQmxCfgSampClkTiming (TaskHandle taskHandle, char *source, float64 rate, int32 edge, int32 mode, uInt64 sampsPerChanToAcquire){
	fprintf (stderr, "Dummy DAQmxCfgSampClkTiming (%d, %s, %f, %d, %d, %lld).\n", taskHandle, source, rate, edge, mode, (long long)sampsPerChanToAcquire);
	settings_rate = rate;  /* globals */
	settings_edge = edge;
	settings_mode = mode;
	samples_remaining_in_task = sampsPerChanToAcquire;
	return(0);
}	
int DAQmxGetSampClkRate (TaskHandle taskHandle, float64 *freq_hz){
	fprintf (stderr, "Dummy DAQmxGetSampClkRate (%d).\n", taskHandle); 
	*freq_hz = settings_rate; /* global */
	return(0);
}	
int DAQmxGetAIEnhancedAliasRejectionEnable (TaskHandle taskHandle, char *physicalchannel, int32 *lfear){
	fprintf (stderr, "Dummy DAQmxGetAIEnhancedAliasRejectionEnable (%d, %s).\n", taskHandle, physicalchannel); 
	*lfear = settings_lfear; /* global */
	return(0);
}	
int DAQmxSetAIEnhancedAliasRejectionEnable (TaskHandle taskHandle, char *physicalchannel, int32 lfear){
	fprintf (stderr, "Dummy DAQmxSetAIEnhancedAliasRejectionEnable (%d, %s, %d).\n", taskHandle, physicalchannel, lfear); 
	settings_lfear = lfear;
	return(0);
}
int DAQmxSetReadReadAllAvailSamp (TaskHandle taskHandle, bool32 readall){
	fprintf (stderr, "Dummy DAQmxSetReadReadAllAvailSamp (%d, %d).\n", taskHandle, readall); 
	will_read_all_available = readall;  /* global */
	return(0);
}	

/* Get other information */
int DAQmxGetBufInputOnbrdBufSize(TaskHandle taskHandle, uInt32 *onboard_buffer){
	fprintf (stderr, "Dummy DAQmxGetBufInputOnbrdBufSize (%d).\n", taskHandle);
	*onboard_buffer = 100000;
	return (0);
}
int DAQmxGetBufInputBufSize(TaskHandle taskHandle, uInt32 *input_buffer){
	fprintf (stderr, "Dummy DAQmxGetBufInputBufSize (%d).\n", taskHandle);
	*input_buffer = 200000;
	return (0);
}
/* Check for overload. */
int DAQmxGetReadOverloadedChansExist (TaskHandle taskHandle, bool32 *overload_occurred){
	fprintf (stderr, "Dummy DAQmxGetReadOverloadedChansExist (%d).\n", taskHandle);
	*overload_occurred = FALSE;
	return (0);
}
int DAQmxGetReadOverloadedChans (TaskHandle taskHandle, char *error_buf, int size){
	error_buf = "Dummy overload error message";
	fprintf (stderr, "Dummy DAQmxGetReadOverloadedChans (%d, %s, %d).\n", taskHandle, error_buf, size);
	return (0);
}


/* Read (dummy) data */
int DAQmxReadAnalogF64(TaskHandle taskHandle, int32 numrequested, float64 timeout, bool32 fillmode, float64 *data, uInt32 size, int32 *numread, bool32 *reserved __attribute__ ((unused)) ){
	int samples_to_fake = 0;
	if (numrequested < DAQmx_Val_Auto){	/* check for invalid. Can't happen, but squelches spurious compiler warning */
		fprintf (stderr, "Dummy DAQmxReadAnalogF64 requested invalid %d.\n", numrequested);
		exit (1);
	}else if (numrequested == DAQmx_Val_Auto){
		if (will_read_all_available == TRUE){  /* <-- global */
			samples_to_fake = 1;	/* Read all the samples in the current buffer. 1 is the safest number to pick, because it can't ever be larger than expected. */
		}else{
			samples_to_fake = samples_remaining_in_task;	/* Read all the samples remaining that we "owe". (ignore the timeout - we're pretending to be super-fast). */
		}
	}else{
		samples_to_fake = numrequested; /* Read the number that were requested (assume the calling function isn't lying) */
	}
	if (samples_to_fake <= 0){
		fprintf (stderr, "Dummy DAQmxReadAnalogF64 invalid samples to fake %d.\n", samples_to_fake);
		exit (1);			
	}
	samples_remaining_in_task -= samples_to_fake; /* <-- global */
	*numread = samples_to_fake;
	for (int i=0; i < samples_to_fake; i++){
		for (int c=0; c< settings_num_channels; c++){ 
			data[settings_num_channels*i+c] = 5 + (i%10)/10.0;	/* make up some plausible data. (fillmode is irrelevant, we're making it up either way) */
		}
	}
	fprintf (stderr, "Dummy DAQmxReadAnalogF64 (%d, %d, %f, %d, %d), returning %d samples.\n", taskHandle, numrequested, timeout, fillmode, size, (int)samples_to_fake);
	return (0);
}
int DAQmxReadBinaryI32(TaskHandle taskHandle, int32 numrequested, float64 timeout, bool32 fillmode, int32 *data_i, uInt32 size, int32 *numread, bool32 *reserved __attribute__ ((unused)) ){
	int samples_to_fake = 0;
	if (numrequested < DAQmx_Val_Auto){	/* check for invalid. Can't happen, but squelches spurious compiler warning */
		fprintf (stderr, "Dummy DAQmxReadBinaryI32 requested invalid %d.\n", numrequested);
		exit (1);
	}else if (numrequested == DAQmx_Val_Auto){
		if (will_read_all_available == TRUE){  /* <-- global */
			samples_to_fake = 1;	/* Read all the samples in the current buffer. 1 is the safest number to pick. */
		}else{
			samples_to_fake = samples_remaining_in_task;	/* Read all the samples remaining that we "owe". */
			if (samples_to_fake <= 0){
				fprintf (stderr, "Dummy DAQmxReadBinaryI32 invalid samples to fake %d.\n", samples_to_fake);
				exit (1);			
			}
		}
	}else{
		samples_to_fake = numrequested; /* Read the number that were requested (assume the calling function isn't lying) */
	}
	if (samples_to_fake <= 0){
		fprintf (stderr, "Dummy DAQmxReadAnalogF64 invalid samples to fake %d.\n", samples_to_fake);
		exit (1);			
	}
	samples_remaining_in_task -= samples_to_fake; /* <-- global */
	*numread = samples_to_fake;
	for (int i=0; i < samples_to_fake; i++){
		for (int c=0; c< settings_num_channels; c++){ 
			data_i[settings_num_channels*i+c] = 5000 + (i%10);	/* make up some plausible data  (fillmode is irrelevant, we're making it up either way) */
		}
	}
	fprintf (stderr, "Dummy DAQmxReadBinaryI32 (%d, %d, %f, %d, %d), returning %d samples.\n", taskHandle, numrequested,  timeout, fillmode, size, (int)samples_to_fake);
	return (0);
}