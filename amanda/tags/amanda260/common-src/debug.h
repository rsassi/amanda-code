/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: debug.h 6789 2007-06-18 20:18:52Z dustin $
 *
 * Logging support
 */

/* this file is included from amanda.h; there is no need to include
 * it explicitly in source files. */

#ifndef AMANDA_DEBUG_H
#define AMANDA_DEBUG_H

/*
 * GENERAL LOGGING
 */

/* Amanda uses glib's logging facilities.  See
 *  http://developer.gnome.org/doc/API/2.2/glib/glib-Message-Logging.html
 *
 * Note that log output will go to stderr until debug_open is called.
 *
 * The error levels are assigned as follows:
 *  g_error -- errors that should dump core (will not return)
 *  g_critical -- fatal errors, exiting with exit status in 
 *    error_exit_status() (will not return)
 *  g_warning -- non-fatal problems
 *  g_message -- normal status information
 *  g_info -- helpful extra details, but not verbose
 *  g_debug -- debug messages
 *
 * g_error and g_critical will respect erroutput_type, potentially
 * sending the error to the Amanda logfile for this run (see logfile.c).
 */

/* g_debug was introduced in glib 2.6, so define it here for systems where
 * it is lacking.  g_info doesn't exist even in glib 2.13, but maybe it will
 * be invented soon..
 */

#ifndef g_debug
#define g_debug(...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif

#ifndef g_info
#define g_info(...) g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#endif

/*
 * FATAL ERROR HANDLING
 */

/* for compatibility; these should eventually be substituted throughout
 * the codebase.  Extra calls to exit() and abort() should be optimized
 * away, and are there only for stupid compilers. */
#define errordump(...) do { g_error(__VA_ARGS__); abort(); } while (0)
#define error(...) do { g_critical(__VA_ARGS__); exit(error_exit_status); } while (0)

/* Additional handling for error and critical messages. */
typedef enum {
    /* send message to stderr (for interactive programs) */
    ERR_INTERACTIVE	= 1 << 0, /* (default) */

    /* log to syslog */
    ERR_SYSLOG		= 1 << 1,

    /* add an L_FATAL entry in the Amanda logfile for the 
     * current run */
    ERR_AMANDALOG	= 1 << 2
} erroutput_type_t;
extern erroutput_type_t erroutput_type;

/* The process exit status that will be given when error()
 * or errordump() is called.
 */
extern int error_exit_status;

/* Supply a pointer to the logfile module's logerror(), if
 * ERR_AMANDALOG is set.
 *
 * This function is required because libamanda, which contains
 * debug.c, is not always linked with the logerror module 
 * (which only appears in server applications).
 *
 * @param logerror_fn: function pointer
 */
void set_logerror(void (*logerror_fn)(char *));

/*
 * DEBUG LOGGING
 */

/* short names */
#define dbopen(a)	debug_open(a)
#define dbreopen(a,b)	debug_reopen(a,b)
#define dbrename(a,b)	debug_rename(a,b)
#define dbclose()	debug_close()
#define dbprintf	debug_printf
#define dbfd()		debug_fd()
#define dbfp()		debug_fp()
#define dbfn()		debug_fn()

/* constants for db(re)open */
#define DBG_SUBDIR_SERVER  "server"
#define DBG_SUBDIR_CLIENT  "client"
#define DBG_SUBDIR_AMANDAD "amandad"

/* Open the debugging log in the given subdirectory.  Once 
 * this function is called, debug logging is available.
 *
 * The debugging file is created in the given subdirectory of the
 * amanda debugging directory, with a filename based on the current
 * process name (from get_pname).
 *
 * @param subdir: subdirectory in which to create the debug file.
 * This is usually one of the DBG_SUBDIR_* constants.  
 */
void	debug_open(char *subdir);

/* Re-open a previously debug_close()d debug file, given by 
 * filename, optionally adding a notation as to why it was
 * reopened.
 *
 * @param file: the filename of the debug file to reopen
 * @param notation: reason for re-opening the file
 */
void	debug_reopen(char *file, char *notation);

/* Rename the debugging logfile into a configuration-specific subdirectory
 * of SUBDIR.  Any existing content of the file will be preserved.
 *
 * @param config: configuration name
 * @param subdir: subdirectory in which to create the debug file.
 */
void	debug_rename(char *config, char *subdir);

/* Flush and close the debugging logfile.  Call this function at application
 * shutdown.
 */
void	debug_close(void);

/* Add a message to the debugging logfile.  A newline is not automatically 
 * added.
 *
 * This function is deprecated in favor of glib's g_debug().
 */
void	debug_printf(const char *format, ...) G_GNUC_PRINTF(1,2);

/* Get the file descriptor for the debug file
 *
 * @returns: the file descriptor
 */
int	debug_fd(void);

/* Get the stdio file handle for the debug file.
 *
 * @returns: the file handle
 */
FILE *	debug_fp(void);

/* Get the pathname of the debug file.
 *
 * The result should not be freed by the caller.
 *
 * @returns: the pathname
 */
char *	debug_fn(void);

/* Use 'dup2' to send stderr output to the debug file.  This is useful
 * when launching other applications, where the stderr of those applications
 * may be necessary for debugging.  It should be called in the child, after
 * the fork().
 */
void debug_dup_stderr_to_debug(void);

/*
 * PROCESS NAME
 */

/*
 * ASSERTIONS
 */

#ifndef SWIG
#ifdef ASSERTIONS

/* Like the standard assert(), but call g_error() to log the result properly */
#define assert(exp)	do {						\
    if (!(exp)) {							\
	g_error(_("assert: %s is false: file %s, line %d"),		\
	   stringize(exp), __FILE__, __LINE__);				\
        g_assert_not_reached();						\
    }									\
} while (0)

#else	/* ASSERTIONS */

#define assert(exp) ((void)0)

#endif	/* ASSERTIONS */
#endif	/* SWIG */

#endif /* AMANDA_DEBUG_H */
