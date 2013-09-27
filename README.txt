INTRO
-----

These programs make use of the NI 4462, National Instruments 4 channel 24-bit 204.8 kHz ADC.

   * ni4462_check
			- Trivial shell-script to check for the presence of the NI 4462 device (by lspci)

   * ni4462_test
			- For experimentation. It configures the NI 4462 in various modes, setting up the voltage-range,
			  input-channel(s), sample-frequency, sample-count, input-coupling, terminal-mode and triggering. It is extremely well
			  commented, and designed partly as an exploration of the NI DAQmx C-library. It is fully-functional for data-capture
			  (though it only uses the Voltage mode, and doesn't exhaustively support every permutation).

   * pb_ni4462_trigger
			- Programs the Pulseblaster to trigger the NI 4462 via digital 'loopback' cable between them. (variable rate and pulse count)

   * pb_ni4462_pulse
			- This sends a trigger and an analog input pulse (with variable offset and length). Uses analog loopback cable.

   * pb_convey_hwtrigger
			- This uses the NI4462's RTSI6 output to constantly convey the HW_Trigger to the PulseBlaster.

   * ni4462_voltmeter
			- Wrapper around ni4462_test to act like a voltmeter.

   * ni4462_speak_noise
			- Simple wrapper around ni4462_test, that causes the noise std-dev to be spoken once per second. Useful for hunting
			  sources of noise: use as a hands-free "multimeter".

   * ni4462_capture
			- Main program used by the IR camera. Less customisable than ni4462_test. Outputs statistical summary data, rather than raw reads.

   * ni4462_system_clock_calibrate
			- Check the master system clock (exported from NI4462, used by PulseBlaster) against an external gate.

   * ni4462_characterise
			- Measure the exact value of the trigger delay, and characterise the ADC input pulse-response.



Each program has a short man page, but should be invoked with "-h" for detailed help.



COMPILE
-------

Simply make, make install.

Note that the (proprietary) libnidaqmx must be installed already. It probably only works on a 32-bit system.

[Alternatively, build with the daqmx_dummy.c file ("make dummy") - this supports a limited subset of fake DAQmx() calls to allow ni4462_test to build, 
even on 64-bit without having libnidaqmx installed.]


DOCUMENTATION
-------------

See:

   *  doc/NOTES.txt
			- my notes on the performance, quirks and limitations of the NI 4462 and DAQmx.

   *  ../../ni-daqmx/
			- the installation of the kernel driver and c-library, libnidaqmx

   *  ../../ni-daqmx/datasheets
			- the NI data acquisition manual and NI 4462 specifications.
			 "NI4462_NI_dynamic_signal_acquisition_manual.pdf",  "NI4462_NI446x_specifications.pdf",
                         "NI4462_24-Bit_204.8kHz_signal_acquisition_brochure.pdf"


   *  /usr/local/natinst/nidaqmx/docs/cdaqmx.chm/_main.html
			- the DAQmx documentation: DAQmx concepts, C-functions, C-properties



SEE ALSO
--------

   * My programs:   linregplot, fftplot, dataplot, dat2wav

   * NI basic utilities: nilsdev, nidaqmxconfig, DAQmxTestPanels

   * LabView (a rather weird, proprietary and expensive GUI wrapper for making simple things complicated!)
     [LabView for Linux is really expensive; LabView for Windows XP has a student edition, installed on the Windows partition of this machine.]




TODO
----

It would be useful to support a "dummy mode" - compile-time choice to not link against libdaqmx, but to redefine the functions as stubs
cf. pb_utils. Then we could run on test-systems where libdaqmx isn't installed, and generate dummy simulated data. Quite a lot of 
functions to emulate though! (So far, this is partially implemented, but only for ni4462_test.c).
