#! @SHELL@
#
# ICEM internal :-)
#

prefix="@prefix@"
exec_prefix="@exec_prefix@"
sbindir="@sbindir@"
amlibexecdir="@amlibexecdir@"
. "${amlibexecdir}/amanda-sh-lib.sh"

ConfigDir=@CONFIG_DIR@

# add sbin and ucb dirs
PATH="$PATH:/usr/sbin:/sbin:/usr/ucb"
export PATH

Program=`basename $0`

log () {
	echo 1>&2 "$@"
	return 0
}

Config=$1
if [ "$Config" = "" ]; then
	log "usage: ${Program} <config> <volume header>"
	exit 1
fi
VOLHEADER=$2
if [ "$VOLHEADER" = "" ]; then
	log "usage: ${Program} <config> <volume header>"
	exit 1
fi

#
# Check if the configuration directory exists.  Make sure that the
# necessary files can be found, such as amanda.conf and tapelist.
#
if [ ! -d ${ConfigDir}/${Config} ]; then
	log "${Program}: configuration directory ${ConfigDir}/${Config} does not exist."
	exit 1
fi
(cd ${ConfigDir}/${Config} >/dev/null 2>&1) || exit $?
cd ${ConfigDir}/${Config}
if [ ! -r amanda.conf ]; then
	log "${Program}: amanda.conf not found or is not readable in ${ConfigDir}."
	exit 1
fi

# Get the location and name of the tapelist filename.  If tapelist is not
# specified in the amanda.conf file, then use tapelist in the config
# directory.
TapeList=`amgetconf${SUF} tapelist`
if [ ! "$TapeList" ]; then
	TapeList="$ConfigDir/$Config/tapelist"
fi
if [ ! -r $TapeList ]; then
	log "${Program}: $TapeList not found or is not readable."
	exit 1
fi

AllTapeList="${TapeList}.all"

[ ! -f $AllTapeList ] \
	&& echo `_ 'no tapelist %s found' "$AllTapeList"` >&2 \
	&& cat $TapeList | sed 's/^.* //' | sort -u > $AllTapeList \
	&& echo `_ '%s created' "$AllTapeList"` >&2

cat $TapeList | sed 's/^.* //' > $AllTapeList.n
cat $AllTapeList >> $AllTapeList.n
sort -u $AllTapeList.n > $AllTapeList && rm -f $AllTapeList.n

MAXTAPE=`cat $AllTapeList | sed "s/^$VOLHEADER//" | sort -n | tail -1`
NEXTTAPE=`expr $MAXTAPE + 1`
echo `_ 'last Tape is %s' "${VOLHEADER}${MAXTAPE}"` >&2

I=1
while [ $I -le $MAXTAPE ]; do
	WRITTEN=`grep "${VOLHEADER}${I}\$" $TapeList`
	EXISTS=`grep "${VOLHEADER}${I}\$" $AllTapeList`
	if [ "$EXISTS" = "" -a "$WRITTEN" = "" ]; then
		echo `_ 'missing tape %s' "${VOLHEADER}${I}"` >&2
		echo "${VOLHEADER}${I}" >> $AllTapeList \
		&& echo `_ 'added %s to %s' "${VOLHEADER}${I}" "$AllTapeList"` >&2
	elif [ "$EXISTS" = "" -a "$WRITTEN" != "" ]; then
		echo `_ 'tape written, but not existing: %s' "${VOLHEADER}${I}"` >&2
	elif [ "$EXISTS" != "" -a "$WRITTEN" = "" ]; then
		echo `_ 'free tape: %s' "${VOLHEADER}${I}"` >&2
	fi
	I=`expr $I + 1`
done

echo `_ 'next tape is %s' "${VOLHEADER}${NEXTTAPE}"` >&2

exit 0
