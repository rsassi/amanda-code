#!@SHELL@
#
# Amanda, The Advanced Maryland Automatic Network Disk Archiver
# Copyright (c) 1991-1998 University of Maryland at College Park
# All Rights Reserved.
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation, and that the name of U.M. not be used in advertising or
# publicity pertaining to distribution of the software without specific,
# written prior permission.  U.M. makes no representations about the
# suitability of this software for any purpose.  It is provided "as is"
# without express or implied warranty.
#
# U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
# BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
# OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# Author: James da Silva, Systems Design and Analysis Group
#			   Computer Science Department
#			   University of Maryland at College Park
#

#
# amdump: Manage running one night's Amanda dump run.
#

prefix="@prefix@"
exec_prefix="@exec_prefix@"
sbindir="@sbindir@"
libexecdir="@libexecdir@"
. "${libexecdir}/amanda-sh-lib.sh"

confdir=@CONFIG_DIR@

# add sbin and ucb dirs
PATH="$PATH:/usr/sbin:/sbin:/usr/ucb"
export PATH

USE_VERSION_SUFFIXES="@USE_VERSION_SUFFIXES@"
if test "$USE_VERSION_SUFFIXES" = "yes"; then
	SUF="-@VERSION@"
else
	SUF=
fi

if [ $# -lt 1 ]
then
        echo `_ 'Usage: %s config [host [disk...]...]' "$0"`  1>&2
        exit 1
fi

exit_status=0;

conf=$1
if [ ! -d $confdir/$conf ]; then
    echo `_ '%s: could not find directory %s' "amdump$SUF" "$confdir/$conf"` 1>&2
    exit 1
fi
shift

cd $confdir/$conf || exit 1

logdir=`amgetconf$SUF $conf logdir "$@"`
[ $? -ne 0 ]  && exit 1
errfile=$logdir/amdump
tapecycle=`amgetconf$SUF $conf tapecycle "$@"`
[ $? -ne 0 ]  && exit 1
dumpuser=`amgetconf$SUF $conf dumpuser "$@"`
[ $? -ne 0 ]  && exit 1

runuser=`{ whoami ; } 2>/dev/null`
if [ $? -ne 0 ]; then
	idinfo=`{ id ; } 2>/dev/null`
	if [ $? -ne 0 ]; then
		runuser=${LOGNAME:-"??unknown??"}
	else
		runuser=`echo $idinfo | sed -e 's/).*//' -e 's/^.*(//'`
	fi
fi

if [ $runuser != $dumpuser ]; then
	echo `_ '%s: must be run as user %s, not %s' "$0" "$dumpuser" "$runuser"` 1>&2
	exit 1
fi

if test -f hold; then
	echo `_ '%s: waiting for hold file to be removed' "$0"` 1>&2
	while test -f hold; do
		sleep 60
	done
fi

if test -f $errfile || test -f $logdir/log; then
	echo `_ '%s: amdump or amflush is already running, or you must run amcleanup' "$0"` 1>&2
	exit 1
fi

umask 077

exit_code=0
# Plan and drive the dumps.
#exec </dev/null >$errfile 2>&1
touch $errfile
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code
exec </dev/null 2>>$errfile 1>&2
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code
printf '%s: start at %s\n' "amdump" "`date`"
printf '%s: datestamp %s\n' "amdump" "`date +%Y%m%d`"
printf '%s: starttime %s\n' "amdump" "`date +%Y%m%d%H%M%S`"
printf '%s: starttime-locale-independent %s\n' "amdump" "`date +'%Y-%m-%d %H:%M:%S %Z'`"
$libexecdir/planner$SUF $conf "$@" | $libexecdir/driver$SUF $conf "$@"
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code
printf '%s: end at %s\n' "amdump" "`date`"

# Send out a report on the dumps.
$sbindir/amreport$SUF $conf "$@"
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code

# Roll the log file to its datestamped name.
$libexecdir/amlogroll$SUF $conf "$@"
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code

# Trim the log file to those for dumps that still exist.
$libexecdir/amtrmlog$SUF $conf "$@"
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code

# Trim the index file to those for dumps that still exist.
$libexecdir/amtrmidx$SUF $conf "$@"
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code

# Keep a debug log through the tapecycle plus a couple of days.
maxdays=`expr $tapecycle + 2`
days=1
# First, find out the last existing errfile,
# to avoid ``infinite'' loops if tapecycle is infinite
while [ $days -lt $maxdays ] && [ -f $errfile.$days ]; do
	days=`expr $days + 1`
done
# Now, renumber the existing log files
while [ $days -ge 2 ]; do
	ndays=`expr $days - 1`
	mv $errfile.$ndays $errfile.$days
	exit_code=$?
	echo $exit_code
	[ $exit_code -ne 0 ] && exit_status=$exit_code
	days=$ndays
done
mv $errfile $errfile.1
exit_code=$?
[ $exit_code -ne 0 ] && exit_status=$exit_code

exit $exit_status
