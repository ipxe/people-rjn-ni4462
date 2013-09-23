These two were nearly finshed but not tested. They are now obsolete.

The problem was that the NI4462 cannot retrigger fast (or even tolerably quickly). So it looked like we'd have to run
for 5 seconds with perfect sync.

BUT... the PulseBlaster and NI 4462 had their own independent internal 100 MHz clocks. 2x 20ppm accuracy... not good enough.

Keeping those in sync by dead reckoning and good luck was just about possible - but only if we could slightly tweak the NI's clock.
(Actually, we can't tweak the clock, but we CAN tweak the sample-rate, which is adjustable with surprisingly very fine precision)

The plan was therefore to measure very carefully when the two started, how soon they went out of sync, and to correct it.
Hence these two programs.


BUT, a far more elegant solution is to actually export the master 100 MHz osc from the NI to the PB. This was relatively
easy to do (FIN1001/1002 LVDS), albeit with a little soldering to the NI board!
