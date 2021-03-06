# SYNOPSIS
#
#   Get default settings for various configuration and command-line options:
#	--with-index-server
#	    define and substitute DEFAULT_SERVER
#	--with-config
#	    define and substitute DEFAULT_CONFIG
#	--with-tape-server
#	    define and substitute DEFAULT_TAPE_SERVER
#	--with-tape-device
#	    define and substitute DEFAULT_TAPE_DEVICE; substitue EXAMPLE_TAPEDEV
#	--with-changer-device
#	    define and substitute DEFAULT_CHANGER_DEVICE
#	--with-amandates
#	    define and substitute DEFAULT_AMANDATES_FILE
#
AC_DEFUN([AMANDA_SETUP_DEFAULTS],
[
    AC_ARG_WITH(index-server,
	AS_HELP_STRING([--with-index-server=HOST],
	    [default amanda index server (default: `uname -n`)]),
       [
	    case "$withval" in
	    "" | y | ye | yes | n | no)
		AC_MSG_ERROR([*** You must supply an argument to the --with-index-server option.])
	      ;;
	    *) DEFAULT_SERVER="$withval"
	      ;;
	    esac
       ],
       : ${DEFAULT_SERVER=`uname -n`}
    )
    AC_DEFINE_UNQUOTED(DEFAULT_SERVER,"$DEFAULT_SERVER",
	[This is the default Amanda index server.])
    AC_SUBST(DEFAULT_SERVER)

    AC_ARG_WITH(config,
	AS_HELP_STRING([--with-config=CONFIG],
	    [default amanda configuration (default: DailySet1)]),
	[
	    case "$withval" in
	    "" | y | ye | yes | n | no)
		AC_MSG_ERROR([*** You must supply an argument to the --with-config option.])
	      ;;
	    *) DEFAULT_CONFIG="$withval"
	      ;;
	    esac
	],
	: ${DEFAULT_CONFIG=DailySet1}
    )
    AC_DEFINE_UNQUOTED(DEFAULT_CONFIG,"$DEFAULT_CONFIG",
	[This is the default Amanda configuration.])
    AC_SUBST(DEFAULT_CONFIG)

    AC_ARG_WITH(tape-server,
	AS_HELP_STRING([--with-tape-server=HOST],
	    [default tape server for restore (default: same as index-server)]),
	[
	    case "$withval" in
	    "" | y | ye | yes | n | no)
		AC_MSG_ERROR([*** You must supply an argument to the --with-tape-server option.])
	      ;;
	    *) DEFAULT_TAPE_SERVER="$withval"
	      ;;
	    esac
	],
	: ${DEFAULT_TAPE_SERVER=$DEFAULT_SERVER}
    )
    AC_DEFINE_UNQUOTED(DEFAULT_TAPE_SERVER,"$DEFAULT_TAPE_SERVER",
	[This is the default restoring Amanda tape server. ])
    AC_SUBST(DEFAULT_TAPE_SERVER)

    AC_ARG_WITH(tape-device,
	AS_HELP_STRING([--with-tape-device=DEVICE],
	    [default device on restore tape server]),
	[
	    case "$withval" in
	    "" | y | ye | yes | n | no)
		AC_MSG_ERROR([*** You must supply an argument to the --with-tape-device option.])
	      ;;
	    *) DEFAULT_TAPE_DEVICE="$withval"
	      ;;
	    esac
	]
    )

    AC_DEFINE_UNQUOTED(DEFAULT_TAPE_DEVICE,"$DEFAULT_TAPE_DEVICE",
	[This is the default no-rewinding tape device. ])
    AC_SUBST(DEFAULT_TAPE_DEVICE)

    if test "${DEFAULT_TAPE_DEVICE+set}" = "set"; then
	EXAMPLE_TAPEDEV="$DEFAULT_TAPE_DEVICE"
    else
	EXAMPLE_TAPEDEV="tape:/dev/YOUR-TAPE-DEVICE-HERE"
    fi
    AC_SUBST(EXAMPLE_TAPEDEV)

    AC_ARG_WITH(changer-device,
	AS_HELP_STRING([--with-changer-device=DEV],
	    [default tape changer device (default: /dev/ch0)]),
	[
	    case "$withval" in
	    "" | y | ye | yes | n | no)
		AC_MSG_ERROR([*** You must supply an argument to the --with-changer-device option.])
	      ;;
	    *) DEFAULT_CHANGER_DEVICE="$withval"
	      ;;
	    esac
	]
    )

    if test -z "$DEFAULT_CHANGER_DEVICE"; then
	DEFAULT_CHANGER_DEVICE=/dev/null
	if test -f /dev/ch0; then
	    DEFAULT_CHANGER_DEVICE=/dev/ch0
	fi
    fi

    AC_DEFINE_UNQUOTED(DEFAULT_CHANGER_DEVICE,"$DEFAULT_CHANGER_DEVICE",
	[This is the default changer device. ])
    AC_SUBST(DEFAULT_CHANGER_DEVICE)

    AC_ARG_WITH(amandates,
        AS_HELP_STRING([--with-amandates],
            [default location for 'amandates' (default: $localstatedir/amanda/amandates)]),
	    [
	    case "$withval" in
	        n | no) AC_MSG_ERROR([*** --without-amandates is not allowed.]);;
	        y |  ye | yes) amandates='$localstatedir/amanda/amandates' ;;
	        *) amandates="$withval";;
	    esac
	    ],
	    [amandates='$localstatedir/amanda/amandates']
    )

    AC_DEFINE_DIR([DEFAULT_AMANDATES_FILE], [amandates],
        [Default location for 'amandates'])
])
