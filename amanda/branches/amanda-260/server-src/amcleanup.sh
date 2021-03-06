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
# amcleanup.sh - clean up and generate a report after a crash.

prefix="@prefix@"
exec_prefix="@exec_prefix@"
sbindir="@sbindir@"
amlibexecdir="@amlibexecdir@"
. "${amlibexecdir}/amanda-sh-lib.sh"

confdir=@CONFIG_DIR@

# add sbin and ucb dirs
PATH="$PATH:/usr/sbin:/sbin:/usr/ucb"
export PATH

USE_VERSION_SUFFIXES="@USE_VERSION_SUFFIXES@"
if test "$USE_VERSION_SUFFIXES" = yes; then
        SUF=-@VERSION@
else
        SUF=
fi

#
#Function to :
#   parse process tree and get the children of a given process ID
#


find_children() {

#sample ps -ef output
#500      20810  4938  0 10:21 pts/2    00:00:00 /bin/sh /usr/sbin/amdump tapebackup

	for pid in $(ps -ef | awk "{if ( \$3 == $1 ) { print \$2 }}")
	do
		process_name=`ps -e|grep -w ${pid}|awk '{print $4}'`
		echo `_ '%s: Process %s found running at pid #%s.' "amcleanup" "${process_name}" "${pid}"`
		pidname[$i]=$pid
		i=`expr $i + 1`
		find_children $pid
	done
}

#
#Function to :
#   send SIGTERM signal to kill given process ID and check if process still alive 
#   after receiving SIGTERM,  if yes send SIGKILL  
#

function killpid() {

killPID=$1
SIGTERM=15

	echo `_ '%s: Sending process %s the %s signal.' "$0" "${killPID}" "SIGTERM"`
	`kill -${SIGTERM} ${killPID} 2>/dev/null`
	for second in 0 1 2 3 4 ; do
		pid_status=`ps -e|grep -w ${killPID}|grep -v grep |wc -l`
		if [ ${pid_status} -ne 0 ] ; then
			# process is still alive
			sleep 1
		else
			return 0
		fi
	done
	SIGKILL=9
	echo `_ '%s: Sending process %s the %s signal.' "$0" "${killPID}" "SIGKILL"`
	`kill -${SIGKILL} ${killPID} 2>/dev/null`
	sleep 2
	pid_status=`ps -e|grep -w ${killPID}|grep -v grep |wc -l`
	if [ ${pid_status} -ne 0 ] ; then
		return 1
	else
		return 0
	fi
}


# process arguments
KILL_ENABLE=0
VERBOSE=0
while test $# -ge 2; do
    case "$1" in
	-k) KILL_ENABLE=1;;
	-v) VERBOSE=1;;
	*)
	    echo `_ 'Usage: amcleanup [-k] [-v] conf'`
	    exit 1 ;;
    esac
    shift
done
conf="$1"
shift

if test ! -d $confdir/$conf ; then
	echo `_ '%s: could not cd into %s' "amcleanup" "$confdir/$conf"`
	exit 1
fi

#check if amdump/amflush is running for given config  
if test ${KILL_ENABLE} -eq 0 ; then
	for am_process in amdump amflush ; do
		am_pid=`ps -ef|grep -w ${am_process}|grep -w ${conf}|awk '{print $2}'`
		if test ! -z "${am_pid}" ; then
			echo `_ '%s: %s Process is running at PID %s for %s configuration.' "$0" "${am_process}" ${am_pid} ${conf}`
			echo `_ '%s: Use -k option to stop all the process...' "$0"`
			echo `_ 'Usage: amcleanup [-k] conf'`
			exit 0
		fi
	done
fi

cd $confdir/$conf

logdir=`amgetconf$SUF $conf logdir "$@"`
rc=$?
if test $rc -ne 0 ; then
	echo `_ '%s: "%s" exited with status: %s' "amcleanup" "amgetconf$SUF logdir" "$rc"` 1>&2
	exit 1
fi
logfile=$logdir/log
errfile=$logdir/amdump
erramflush=$logdir/amflush
tapecycle=`amgetconf$SUF $conf tapecycle "$@"`
rc=$?
if test $rc -ne 0 ; then
	echo `_ '%s: "%s" exited with status: %s' "amcleanup" "amgetconf$SUF tapecycle" "$rc"` 1>&2
	exit 1
fi
dumpuser=`amgetconf$SUF $conf dumpuser "$@"`
rc=$?
if test $rc -ne 0 ; then
	echo `_ '%s: "%s" exited with status: %s' "amcleanup" "amgetconf$SUF $conf dumpuser" "$rc"` 1>&2
	exit 1
fi
if test ${KILL_ENABLE} -eq 1 ; then

	#check if any one of the dumps are runing, if yes get the process tree and kill them
        for am_process in amdump amflush ; do
                unset pidname
                am_pid=`ps -ef|grep -w ${am_process}|grep -w ${conf}|awk '{print $2}'`
                #echo "checking children for ${am_pid}"
                if test ! -z "${am_pid}" ; then
                        find_children ${am_pid}
                fi

                KILL_FAILURES=0
		i=0

                while test ${#pidname[@]} -gt $i ; do

                        mypid=`ps -e|grep -w ${pidname[$i]}|grep -v grep|wc -l`
                        if [ ${mypid} -ne 0 ] ; then
                                killpid ${pidname[$i]}
                                rc=$?
                                if test $rc -ne 0 ; then
                                        KILL_FAILURES=`expr ${KILL_FAILURES} + 1`
                                fi
                        else
                                echo `_ '%s: Process %s no longer running.  Skipping...' "$0" "${pidname[$i]}"`
                        fi
			i=`expr $i + 1`
                done

        if test ${#pidname[@]} -gt 0 ; then
                echo `_ '%s: %s Amanda processes were found running.' "$0" "${#pidname[@]}"`
                echo `_ '%s: %s processes failed to terminate.' "$0" "${KILL_FAILURES}"`
        fi

        done
fi

retstatus=0
if test -f $logfile ; then
	echo `_ '%s: processing outstanding log file.' "$0"`
	exec </dev/null >/dev/null 2>&1
	amreport$SUF $conf "$@"
	rc=$?
	if test $rc -ne 0 ; then
		echo `_ '%s: "%s" exited with status: %s' "$0" "amreport" "$rc"` 1>&2
		retstatus=`expr $retstatus + 1`
	fi

	# Roll the log file to its datestamped name.
	amlogroll$SUF $conf "$@"
	rc=$?
	if test $rc -ne 0 ; then
		echo `_ '%s: "%s" exited with status: %s' "$0" "amlogroll" "$rc"` 1>&2
		retstatus=`expr $retstatus + 2`
	fi

	# Trim the index file to those for dumps that still exist.
	amtrmidx$SUF $conf "$@"
	rc=$?
	if test $rc -ne 0 ; then
		echo `_ '%s: "%s" exited with status: %s' "$0" "amtrmidx" "$rc"` 1>&2
		retstatus=`expr $retstatus + 4`
	fi

else
	echo `_ '%s: no unprocessed logfile to clean up.' "$0"`
fi

if test -f $errfile ; then
    # if log was found, this will have been directed to /dev/null,
    # which is fine.
    echo `_ '%s: %s exists, renaming it.' "$0" "$errfile"`

    # Keep debug log through the tapecycle plus a couple days
    maxdays=`expr $tapecycle + 2`

    days=1
    # First, find out the last existing errfile,
    # to avoid ``infinite'' loops if tapecycle is infinite
    while test $days -lt $maxdays  && test -f $errfile.$days ; do
	days=`expr $days + 1`
    done
    # Now, renumber the existing log files
    while test $days -ge 2 ; do
	ndays=`expr $days - 1`
	mv $errfile.$ndays $errfile.$days
	days=$ndays
    done
    mv $errfile $errfile.1
fi

if test -f $erramflush ; then
    # if log was found, this will have been directed to /dev/null,
    # which is fine.
    echo `_ '%s: %s exists, renaming it.' "$0" "$erramflush"`

    # Keep debug log through the tapecycle plus a couple days
    maxdays=`expr $tapecycle + 2`

    days=1
    # First, find out the last existing erramflush,
    # to avoid ``infinite'' loops if tapecycle is infinite
    while test $days -lt $maxdays  && test -f $erramflush.$days ; do
	days=`expr $days + 1`
    done
    # Now, renumber the existing log files
    while test $days -ge 2 ; do
	ndays=`expr $days - 1`
	mv $erramflush.$ndays $erramflush.$days
	days=$ndays
    done
    mv $erramflush $erramflush.1
fi

v=''
if test $VERBOSE -eq 1; then
    v='-v'
fi
$amlibexecdir/amcleanupdisk $v $conf "$@"
rc=$?
if test $rc -ne 0 ; then
	echo `_ '%s: "%s" exited with status: %s' "$0" "amcleanupdisk" "$rc"` 1>&2
	retstatus=`expr $retstatus + 8`
fi

exit $retstatus
