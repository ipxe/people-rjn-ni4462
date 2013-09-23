#!/bin/bash
#Check for whether NI 4462 PCI card is physically present. NB the PCI_STRING is for our specific NI PCI model.
#See also nilsdev

PCI_STRING="National Instruments Device 7170"

if [ $# != 0 ]; then
	echo "This checks for the presence of a physical NI 4462  device in this computer's PCI slot."
	echo "It checks:  'lspci | grep \"$PCI_STRING\"'"
	echo "If found, return 0; else return 1."
	exit 1
fi

echo -n "Checking for NI 4462..." 
if lspci | grep -q "$PCI_STRING" ; then
	echo "OK"
	exit 0
else
	echo "ERROR"
	echo "Warning: NI 4462 PCI device not present! Failed to find PCI device '$PCI_STRING'" >&2
	echo "Turn off this PC, remove lid and wiggle the PCI cards carefully..." >&2
	#zenity --error --text="ERROR: NI 4462 PCI device not present! Turn off, remove lid and wiggle the PCI cards carefully."
	exit 1
fi

