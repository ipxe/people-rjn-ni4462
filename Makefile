PREFIX      = $(DESTDIR)/usr/local
BINDIR      = $(PREFIX)/bin
DATAROOTDIR = $(PREFIX)/share
DOCDIR      = $(DATAROOTDIR)/doc/ni4462
MANDIR      = $(DATAROOTDIR)/man
MAN1DIR     = $(MANDIR)/man1
BASHCOMPDIR = /etc/bash_completion.d

CFLAGS      = -Wall -Wextra -Werror -O3 -march=native -std=gnu99
LDFLAGS     = -lnidaqmx -lm
DUMMY       = -DUSE_DUMMY_LIBDAQMX -O2   #The -O2 prevents a wrong warning about "raw[x] may be used uninitialized"
D_LDFLAGS   = -lm

WWW_DIR     = ni4462

all :: ni4462 manpages

ni4462 : ni4462_test ni4462_capture

dummy : ni4462_dummy manpages

ni4462_test:
	@[ "`getconf LONG_BIT`" = "32" ] || echo "*** Warning: this is not a 32-bit system. It probably can't compile using the (32-bit) libnidaqmx binary blob. ***"
	@[ "`getconf LONG_BIT`" = "32" ] || { echo "*** Giving up now.... (to ignore, comment out the second 'getconf' line from '$@' target of the 'ni4462/Makefile'). ***" ; exit 1; }
	$(CC) $(CFLAGS) -o src/ni4462_test src/ni4462_test.c $(LDFLAGS)
	strip src/ni4462_test

ni4462_capture:
	$(CC) $(CFLAGS) -o src/ni4462_capture src/ni4462_capture.c $(LDFLAGS)
	strip src/ni4462_capture

experiments :
	$(CC) $(CFLAGS) -o src/tests/ni4462_bug_dont_use_task_commit src/tests/ni4462_bug_dont_use_task_commit.c $(LDFLAGS)
	$(CC) $(CFLAGS) -o src/tests/ni4462_experiment_readanalogf64_params src/tests/ni4462_experiment_readanalogf64_params.c $(LDFLAGS)
	$(CC) $(CFLAGS) -o src/tests/ni4462_experiment_task_performance src/tests/ni4462_experiment_task_performance.c $(LDFLAGS)

ni4462_dummy :
	@echo "Compiling in dummy mode, without the real libnidaqmx."       
	$(CC) $(CFLAGS) $(DUMMY) -o src/ni4462_test src/ni4462_test.c  $(D_LDFLAGS)
	$(CC) $(CFLAGS) $(DUMMY) -o src/ni4462_capture src/ni4462_capture.c $(D_LDFLAGS)

manpages :  
	bash man/ni4462_test.1.sh
	bash man/ni4462_capture.1.sh
	bash man/ni4462_check.1.sh
	bash man/ni4462_reset.1.sh
	bash man/ni4462_selfcal.1.sh
	bash man/pb_ni4462_trigger.1.sh
	bash man/pb_ni4462_pulse.1.sh
	bash man/pb_convey_hwtrigger.1.sh
	bash man/ni4462_system_clock_calibrate.1.sh
	bash man/ni4462_speak_noise.1.sh
	bash man/ni4462_voltmeter.1.sh
	bash man/ni4462_characterise.1.sh

.PHONY: www
www:
	rm -rf   www .www
	mkdir -p .www/$(WWW_DIR)/$(WWW_DIR)
	cp -r *  .www/$(WWW_DIR)/$(WWW_DIR)
	mv       .www www
	make -C  www/$(WWW_DIR)/$(WWW_DIR) clean
	tar -czf www/$(WWW_DIR)/$(WWW_DIR).tgz -C www/$(WWW_DIR) $(WWW_DIR)
	make -C  www/$(WWW_DIR)/$(WWW_DIR) dummy
	cp       www/$(WWW_DIR)/$(WWW_DIR)/man/*.html  www/$(WWW_DIR)
	rm -rf   www/$(WWW_DIR)/$(WWW_DIR)
	cp       index.html README.txt doc/NOTES.txt www/$(WWW_DIR)/
	@echo "Now, upload www/$(WWW_DIR)/ and link to www/$(WWW_DIR)/index.html"
	
clean :
	rm -f src/ni4462_test 
	rm -f src/ni4462_capture
	rm -f src/tests/ni4462_bug_dont_use_task_commit
	rm -f src/tests/ni4462_experiment_readanalogf64_params
	rm -f src/tests/ni4462_experiment_task_performance
	rm -f man/*.bz2 man/*.html
	rm -rf www
	
install:
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)

	mkdir -p $(BINDIR) $(MAN1DIR) $(DOCDIR)
	install        src/ni4462_test                      $(BINDIR)
	install        src/ni4462_capture                   $(BINDIR)/
	install        src/ni4462_check.sh                  $(BINDIR)/ni4462_check
	install        src/ni4462_reset.sh                  $(BINDIR)/ni4462_reset
	install        src/ni4462_selfcal.sh                $(BINDIR)/ni4462_selfcal
	install        src/ni4462_system_clock_calibrate.sh $(BINDIR)/ni4462_system_clock_calibrate
	install        src/ni4462_characterise.sh           $(BINDIR)/ni4462_characterise
	install        src/pb_ni4462_trigger.sh             $(BINDIR)/pb_ni4462_trigger
	install        src/pb_ni4462_pulse.sh               $(BINDIR)/pb_ni4462_pulse
	install        src/pb_convey_hwtrigger.sh           $(BINDIR)/pb_convey_hwtrigger
	install        src/ni4462_speak_noise.sh            $(BINDIR)/ni4462_speak_noise
	install        src/ni4462_voltmeter.sh              $(BINDIR)/ni4462_voltmeter
	install  -m644 src/ni4462.bashcompletion            $(BASHCOMPDIR)/ni4462
	install  -m644 man/*bz2                             $(MAN1DIR)
	install  -m644 LICENSE.txt README.txt index.html    $(DOCDIR)/
	cp -r    doc/* src/tests/*.c  $(DOCDIR)/

uninstall:
	@[ `whoami` = root ] || (echo "Error, please be root"; exit 1)

	rm -f $(BINDIR)/ni4462_test
	rm -f $(BINDIR)/ni4462_capture
	rm -f $(BINDIR)/ni4462_check
	rm -f $(BINDIR)/ni4462_reset
	rm -f $(BINDIR)/ni4462_selfcal
	rm -f $(BINDIR)/ni4462_characterise
	rm -f $(BINDIR)/pb_ni4462_trigger
	rm -f $(BINDIR)/pb_ni4462_pulse
	rm -f $(BINDIR)/pb_convey_hwtrigger
	rm -f $(BINDIR)/ni4462_system_clock_calibrate
	rm -f $(BINDIR)/ni4462_voltmeter
	rm -f $(BINDIR)/ni4462_pb_speak_noise
	rm -f $(BASHCOMPDIR)/ni4462
	rm -f $(MAN1DIR)/ni4462_test.1.bz2 
	rm -f $(MAN1DIR).ni4462_capture.1.bz2 
	rm -f $(MAN1DIR).ni4462_check.1.bz2 
	rm -f $(MAN1DIR).pb_ni4462_trigger.1.bz2 
	rm -f $(MAN1DIR).pb_ni4462_pulse.1.bz2 
	rm -f $(MAN1DIR).pb_convey_hwtrigger.1.bz2
	rm -f $(MAN1DIR).ni4462_system_clock_calibrate.1.bz2 
	rm -f $(MAN1DIR).ni4462_speak_noise.1.bz2 
	rm -f $(MAN1DIR).ni4462_voltmeter.1.bz2 
	rm -rf $(DOCDIR)
