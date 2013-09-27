#Generate manpage from command's output. Invoke with "sh", -h for help.

#Program name.
NAME="ni4462_test"

#The binary, (relative path to this script). Invoked with "-h" for help text (stdout or stderr)
BINARY=../src/ni4462_test

#Description: brief string for the start of the man page.
DESCRIPTION="capture from the NI4462 card, experiment with parameters"

#Synopsis text, or leave blank to omit. Add leading spaces to avoid automatic paragraph formatting.
SYNOPSIS=`cat <<-EOT
 Sets up the NI4462 device with desired parameters: channel, freq, count, gain, coupling, terminal mode, triggering
 Outputs data in format containing one or more columns of int/float data, and statistics.
 Can also do diagnostics, self-calibration and reset of the device.
 The source (in C) is highly commented as example code for NI DAQmx (libdaqmx).
EOT`

#Section of manual.
SECTION=1

#Program group/source
SOURCE="IR Camera System"

#Time when the manual was written (string).
DATE="July 2012"

#See also. Array, Each manpage with its section.
SEE_ALSO=( "ni4462_check (1)" "dataplot (1)" "linregplot (1)" "fftplot (1)" "dat2wav (1)" "pb_ni4462_trigger (1)" "inotifywait (1)" "ni4462_speak_noise (1)" "pb_convey_hwtrigger (1)" "nilsdev (1)" "DAQmxTestPanels (1)" )

#Prefix each line with a leading space? Prevent paragraphs from being line-wrapped. true/false
LEADING_SPACE=true

#Author and copyright (optional string).
LICENSE="GPL v3+, with exception for linking against libdaqmx"
AUTHOR="The author of $NAME and this manual page is Richard Neill, <ni4462@richardneill.org>"$'\n.br\n'"Copyright $DATE; this is Free Software ($LICENSE), see the source for copying conditions."

# ---- END CONFIGURATION -----

BZIP2_FILE=`dirname $0`/$NAME.$SECTION.bz2
COMPRESS=bzip2
if [ "$1" == -h ]; then echo "This generates the man page for $NAME. Run with no args to create $BZIP2_FILE, use '-' for uncompressed stdout, or specify a filename."; exit 1; fi
if [ "$1" == - ] ;then COMPRESS=cat; BZIP2_FILE=/dev/stdout; elif [ -n "$1" ] ;then BZIP2_FILE=$1; fi

#Generate title and name text.
TITLE=$(echo $NAME | tr '[A-Z]' '[a-z]')" - $DESCRIPTION"
NAME=$(echo $NAME | tr '[a-z]' '[A-Z]')

#Look up section name title.
SECTION_NAMES=( "zero" "User Commands" "System calls" "Library calls" "Special files (devices)" "File formats and conventions" "Games" "Conventions and miscellaneous" "System management commands" )
SECTION_NAME=${SECTION_NAMES[$SECTION]}

#Optional sections Synopsis. Author
[ -n "$SYNOPSIS" ] && SYNOPSIS=".SH SYNOPSIS"$'\n'"$SYNOPSIS"
[ -n "$AUTHOR" ] && AUTHOR=".SH AUTHOR"$'\n'"$AUTHOR"

#Get the help from the binary with -h. It may be on stdout or stderr.
#Double backslashes to prevent groff interpreting eg:  "\fIformattedtext\fR"
#For any line that begins with a dot or single-quote, prefix with the non-printing character '\&'. Otherwise, eg ".I formattedtext" gets interpreted.
#If necessary, prefix each line with " ": prevent groff from wrapping paragraphs. (double-newlines are safe; multiple blank-lines are converted to a single blankline)
[ "$LEADING_SPACE" == true ] && SPACE=" " || SPACE='';
HELPTEXT=$(`dirname $0`/$BINARY -h 2>&1 | sed -e 's/\\/\\\\/g' -e 's/\(^\(\.\|'"'"'\).*\)/\\\&\1/g' -e "s/\(.*\)/$SPACE\1/g")

#Build up the see-also list. ".BR" macro means bold, then roman.
Y=''; for X in "${SEE_ALSO[@]}"; do Y="$Y.BR $X,"$'\n'; done; SEE_ALSO=${Y%,$'\n'}

#Now write out the manual, in nroff format. Bzip.
cat <<-END_OF_MANUAL | $COMPRESS > $BZIP2_FILE
.TH "$NAME" "$SECTION" "$DATE" "$SOURCE" "$SECTION_NAME"
.SH NAME
$TITLE
$SYNOPSIS

.SH DESCRIPTION
$HELPTEXT

$AUTHOR

.SH "SEE ALSO"
$SEE_ALSO
END_OF_MANUAL

#Also create the HTML version,fixing spacing, and munging email addresses.
[ "$1" != "-" ] && cat $BZIP2_FILE | $COMPRESS -d | man2html -r - | tail -n +3 | sed -e 's/<BODY>/<BODY><STYLE>\*\{font-family:monospace\}<\/STYLE>/' -re 's/\b([a-z0-9_.+-]*)@([a-z0-9_.+-]*)\b/\1#AT(spamblock)#\2/ig' > ${BZIP2_FILE%.bz2}.html
