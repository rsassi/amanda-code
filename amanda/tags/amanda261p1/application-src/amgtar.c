/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
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
 * $Id: amgtar.c 8888 2007-10-02 13:40:42Z martineau $
 *
 * send estimated backup sizes using dump
 */

/* PROPERTY:
 *
 * GNUTAR-PATH     (default GNUTAR)
 * GNUTAR-LISTDIR  (default CNF_GNUTAR_LIST_DIR)
 * DIRECTORY       (no default, if set, the backup will be from that directory
 *		    instead of from the --device)
 * ONE-FILE-SYSTEM (default YES)
 * SPARSE          (default YES)
 * ATIME-PRESERVE  (default YES)
 * CHECK-DEVICE    (default YES)
 * INCLUDE-FILE
 * INCLUDE-LIST
 * INCLUDE-OPTIONAL
 * EXCLUDE-FILE
 * EXCLUDE-LIST
 * EXCLUDE-OPTIONAL
 * NORMAL
 * IGNORE
 * STRANGE
 * EXIT-HANDLING   (1=GOOD 2=BAD)
 * TAR-BLOCKSIZE   (default does not add --blocking-factor option,
 *                  using tar's default)
 */

#include "amanda.h"
#include "pipespawn.h"
#include "amfeatures.h"
#include "clock.h"
#include "util.h"
#include "getfsent.h"
#include "version.h"
#include "client_util.h"
#include "conffile.h"
#include "amandad.h"
#include "getopt.h"
#include "sendbackup.h"

int debug_application = 1;
#define application_debug(i, ...) do {	\
	if ((i) <= debug_application) {	\
	    dbprintf(__VA_ARGS__);	\
	}				\
} while (0)

static amregex_t init_re_table[] = {
  /* tar prints the size in bytes */
  AM_SIZE_RE("^ *Total bytes written: [0-9][0-9]*", 1, 1),
  AM_NORMAL_RE("^could not open conf file"),
  AM_NORMAL_RE("^Elapsed time:"),
  AM_NORMAL_RE("^Throughput"),
  AM_IGNORE_RE(": Directory is new$"),
  AM_IGNORE_RE(": Directory has been renamed"),

  /* GNU tar 1.13.17 will print this warning when (not) backing up a
     Unix named socket.  */
  AM_NORMAL_RE(": socket ignored$"),

  /* GNUTAR produces a few error messages when files are modified or
     removed while it is running.  They may cause data to be lost, but
     then they may not.  We shouldn't consider them NORMAL until
     further investigation.  */
  AM_NORMAL_RE(": File .* shrunk by [0-9][0-9]* bytes, padding with zeros"),
  AM_NORMAL_RE(": Cannot add file .*: No such file or directory$"),
  AM_NORMAL_RE(": Error exit delayed from previous errors"),
  
  /* catch-all: DMP_STRANGE is returned for all other lines */
  AM_STRANGE_RE(NULL)
};
static amregex_t *re_table;

/* local functions */
int main(int argc, char **argv);

typedef struct application_argument_s {
    char      *config;
    char      *host;
    int        message;
    int        collection;
    int        calcsize;
    char      *tar_blocksize;
    GSList    *level;
    dle_t      dle;
    int        argc;
    char     **argv;
} application_argument_t;

enum { CMD_ESTIMATE, CMD_BACKUP };

static void amgtar_support(application_argument_t *argument);
static void amgtar_selfcheck(application_argument_t *argument);
static void amgtar_estimate(application_argument_t *argument);
static void amgtar_backup(application_argument_t *argument);
static void amgtar_restore(application_argument_t *argument);
static void amgtar_validate(application_argument_t *argument);
static void amgtar_build_exinclude(dle_t *dle, int verbose,
				   int *nb_exclude, char **file_exclude,
				   int *nb_include, char **file_include);
static char *amgtar_get_incrname(application_argument_t *argument, int level);
static char **amgtar_build_argv(application_argument_t *argument,
				char *incrname, int command);
static amregex_t *build_re_table(amregex_t *orig_re_table,
				 GSList *normal_message,
				 GSList *ignore_message,
				 GSList *strange_message);
static void add_type_table(dmpline_t typ,
			   amregex_t **re_table, amregex_t *orig_re_table,
			   GSList *normal_message, GSList *ignore_message,
			   GSList *strange_message);
static void add_list_table(dmpline_t typ, amregex_t **re_table,
			  GSList *message);
static char *gnutar_path;
static char *gnutar_listdir;
static char *gnutar_directory;
static int gnutar_onefilesystem;
static int gnutar_atimepreserve;
static int gnutar_checkdevice;
static int gnutar_sparse;
static GSList *normal_message = NULL;
static GSList *ignore_message = NULL;
static GSList *strange_message = NULL;
static char   *exit_handling;
static int    exit_value[256];

static struct option long_options[] = {
    {"config"          , 1, NULL,  1},
    {"host"            , 1, NULL,  2},
    {"disk"            , 1, NULL,  3},
    {"device"          , 1, NULL,  4},
    {"level"           , 1, NULL,  5},
    {"index"           , 1, NULL,  6},
    {"message"         , 1, NULL,  7},
    {"collection"      , 0, NULL,  8},
    {"record"          , 0, NULL,  9},
    {"gnutar-path"     , 1, NULL, 10},
    {"gnutar-listdir"  , 1, NULL, 11},
    {"one-file-system" , 1, NULL, 12},
    {"sparse"          , 1, NULL, 13},
    {"atime-preserve"  , 1, NULL, 14},
    {"check-device"    , 1, NULL, 15},
    {"include-file"    , 1, NULL, 16},
    {"include-list"    , 1, NULL, 17},
    {"include-optional", 1, NULL, 18},
    {"exclude-file"    , 1, NULL, 19},
    {"exclude-list"    , 1, NULL, 20},
    {"exclude-optional", 1, NULL, 21},
    {"directory"       , 1, NULL, 22},
    {"normal"          , 1, NULL, 23},
    {"ignore"          , 1, NULL, 24},
    {"strange"         , 1, NULL, 25},
    {"exit-handling"   , 1, NULL, 26},
    {"calcsize"        , 0, NULL, 27},
    {"tar-blocksize"   , 1, NULL, 28},
    {NULL, 0, NULL, 0}
};


void
add_type_table(
    dmpline_t   typ,
    amregex_t **re_table,
    amregex_t  *orig_re_table,
    GSList     *normal_message,
    GSList     *ignore_message,
    GSList     *strange_message)
{
    amregex_t *rp;

    for(rp = orig_re_table; rp->regex != NULL; rp++) {
	if (rp->typ == typ) {
	    int     found = 0;
	    GSList *mes;

	    for (mes = normal_message; mes != NULL; mes = mes->next) {
		if (strcmp(rp->regex, (char *)mes->data) == 0)
		    found = 1;
	    }
	    for (mes = ignore_message; mes != NULL; mes = mes->next) {
		if (strcmp(rp->regex, (char *)mes->data) == 0)
		    found = 1;
	    }
	    for (mes = strange_message; mes != NULL; mes = mes->next) {
		if (strcmp(rp->regex, (char *)mes->data) == 0)
		    found = 1;
	    }
	    if (found == 0) {
		(*re_table)->regex   = rp->regex;
		(*re_table)->srcline = rp->srcline;
		(*re_table)->scale   = rp->scale;
		(*re_table)->field   = rp->field;
		(*re_table)->typ     = rp->typ;
		(*re_table)++;
	    }
	}
    }
}

void
add_list_table(
    dmpline_t   typ,
    amregex_t **re_table,
    GSList     *message)
{
    GSList *mes;

    for (mes = message; mes != NULL; mes = mes->next) {
	(*re_table)->regex = (char *)mes->data;
	(*re_table)->srcline = 0;
	(*re_table)->scale   = 0;
	(*re_table)->field   = 0;
	(*re_table)->typ     = typ;
	(*re_table)++;
    }
}

amregex_t *
build_re_table(
    amregex_t *orig_re_table,
    GSList    *normal_message,
    GSList    *ignore_message,
    GSList    *strange_message)
{
    int        nb = 0;
    amregex_t *rp;
    amregex_t *re_table, *new_re_table;

    for(rp = orig_re_table; rp->regex != NULL; rp++) {
	nb++;
    }
    nb += g_slist_length(normal_message);
    nb += g_slist_length(ignore_message);
    nb += g_slist_length(strange_message);
    nb ++;

    re_table =  new_re_table = malloc(nb * sizeof(amregex_t));
    
    /* add SIZE from orig_re_table */
    add_type_table(DMP_SIZE, &re_table, orig_re_table,
		   normal_message, ignore_message, strange_message);

    /* add ignore_message */
    add_list_table(DMP_IGNORE, &re_table, ignore_message);

    /* add IGNORE from orig_re_table */
    add_type_table(DMP_IGNORE, &re_table, orig_re_table,
		   normal_message, ignore_message, strange_message);

    /* add normal_message */
    add_list_table(DMP_NORMAL, &re_table, normal_message);

    /* add NORMAL from orig_re_table */
    add_type_table(DMP_NORMAL, &re_table, orig_re_table,
		   normal_message, ignore_message, strange_message);

    /* add strange_message */
    add_list_table(DMP_STRANGE, &re_table, strange_message);

    /* add STRANGE from orig_re_table */
    add_type_table(DMP_STRANGE, &re_table, orig_re_table,
		   normal_message, ignore_message, strange_message);

    /* Add DMP_STRANGE with NULL regex,       */
    /* it is not copied by previous statement */
    re_table->regex = NULL;
    re_table->srcline = 0;
    re_table->scale = 0;
    re_table->field = 0;
    re_table->typ = DMP_STRANGE;

    return new_re_table;
}

int
main(
    int		argc,
    char **	argv)
{
    int c;
    char *command;
    application_argument_t argument;
    int i;

#ifdef GNUTAR
    gnutar_path = GNUTAR;
#else
    gnutar_path = NULL;
#endif
    gnutar_directory = NULL;
    gnutar_onefilesystem = 1;
    gnutar_atimepreserve = 1;
    gnutar_checkdevice = 1;
    gnutar_sparse = 1;
    exit_handling = NULL;

    /* initialize */

    /*
     * Configure program for internationalization:
     *   1) Only set the message locale for now.
     *   2) Set textdomain for all amanda related programs to "amanda"
     *      We don't want to be forced to support dozens of message catalogs.
     */  
    setlocale(LC_MESSAGES, "C");
    textdomain("amanda"); 

    /* drop root privileges */
    if (!set_root_privs(0)) {
	error(_("amgtar must be run setuid root"));
    }

    safe_fd(3, 2);

    set_pname("amgtar");

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

#if defined(USE_DBMALLOC)
    malloc_size_1 = malloc_inuse(&malloc_hist_1);
#endif

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    dbopen(DBG_SUBDIR_CLIENT);
    startclock();
    dbprintf(_("version %s\n"), version());

    config_init(CONFIG_INIT_CLIENT, NULL);

    //check_running_as(RUNNING_AS_DUMPUSER_PREFERRED);
    //root for amrecover
    //RUNNING_AS_CLIENT_LOGIN from selfcheck, sendsize, sendbackup

    /* parse argument */
    command = argv[1];

    argument.config     = NULL;
    argument.host       = NULL;
    argument.message    = 0;
    argument.collection = 0;
    argument.calcsize   = 0;
    argument.tar_blocksize = NULL;
    argument.level      = NULL;
    init_dle(&argument.dle);

    while (1) {
	int option_index = 0;
    	c = getopt_long (argc, argv, "", long_options, &option_index);
	if (c == -1) {
	    break;
	}
	switch (c) {
	case 1: argument.config = stralloc(optarg);
		break;
	case 2: argument.host = stralloc(optarg);
		break;
	case 3: argument.dle.disk = stralloc(optarg);
		break;
	case 4: argument.dle.device = stralloc(optarg);
		break;
	case 5: argument.level = g_slist_append(argument.level,
					        GINT_TO_POINTER(atoi(optarg)));
		break;
	case 6: argument.dle.create_index = 1;
		break;
	case 7: argument.message = 1;
		break;
	case 8: argument.collection = 1;
		break;
	case 9: argument.dle.record = 1;
		break;
	case 10: gnutar_path = stralloc(optarg);
		 break;
	case 11: gnutar_listdir = stralloc(optarg);
		 break;
	case 12: if (optarg && strcasecmp(optarg, "YES") != 0)
		     gnutar_onefilesystem = 0;
		 break;
	case 13: if (optarg && strcasecmp(optarg, "YES") != 0)
		     gnutar_sparse = 0;
		 break;
	case 14: if (optarg && strcasecmp(optarg, "YES") != 0)
		     gnutar_atimepreserve = 0;
		 break;
	case 15: if (optarg && strcasecmp(optarg, "YES") != 0)
		     gnutar_checkdevice = 0;
		 break;
	case 16: if (optarg)
		     argument.dle.include_file =
			 append_sl(argument.dle.include_file, optarg);
		 break;
	case 17: if (optarg)
		     argument.dle.include_list =
			 append_sl(argument.dle.include_list, optarg);
		 break;
	case 18: argument.dle.include_optional = 1;
		 break;
	case 19: if (optarg)
		     argument.dle.exclude_file =
			 append_sl(argument.dle.exclude_file, optarg);
		 break;
	case 20: if (optarg)
		     argument.dle.exclude_list =
			 append_sl(argument.dle.exclude_list, optarg);
		 break;
	case 21: argument.dle.exclude_optional = 1;
		 break;
	case 22: gnutar_directory = stralloc(optarg);
		 break;
	case 23: if (optarg)
		     normal_message = 
			 g_slist_append(normal_message, optarg);
		 break;
	case 24: if (optarg)
		     ignore_message = 
			 g_slist_append(ignore_message, optarg);
		 break;
	case 25: if (optarg)
		     strange_message = 
			 g_slist_append(strange_message, optarg);
		 break;
	case 26: if (optarg)
		     exit_handling = stralloc(optarg);
		 break;
	case 27: argument.calcsize = 1;
		 break;
	case 28: argument.tar_blocksize = stralloc(optarg);
	case ':':
	case '?':
		break;
	}
    }

    argument.argc = argc - optind;
    argument.argv = argv + optind;

    if (argument.config) {
	config_init(CONFIG_INIT_CLIENT | CONFIG_INIT_EXPLICIT_NAME | CONFIG_INIT_OVERLAY,
		    argument.config);
	dbrename(get_config_name(), DBG_SUBDIR_CLIENT);
    }

    if (config_errors(NULL) >= CFGERR_ERRORS) {
	g_critical(_("errors processing config file"));
    }

    re_table = build_re_table(init_re_table, normal_message, ignore_message,
			      strange_message);

    for(i=0;i<256;i++)
	exit_value[i] = 1; /* BAD  */
    exit_value[0] = 0;     /* GOOD */
    exit_value[1] = 0;     /* GOOD */
    if (exit_handling) {
	char *s = exit_handling;
	while (s) {
	    char *r = strchr(s, '=');
	    if (r) {
		int j = atoi(s);
		if (j >= 0 && j < 256) {
		    r++;
		    if (strncasecmp(r, "GOOD", 4) == 0) {
			exit_value[j] = 0;
		    }
		}
	    }
	    s = strchr(s+1, ' ');
	}
    }

    gnutar_listdir = getconf_str(CNF_GNUTAR_LIST_DIR);
    if (strlen(gnutar_listdir) == 0)
	gnutar_listdir = NULL;

    if (gnutar_path) {
	dbprintf("GNUTAR-PATH %s\n", gnutar_path);
    } else {
	dbprintf("GNUTAR-PATH is not set\n");
    }
    if (gnutar_listdir) {
	    dbprintf("GNUTAR-LISTDIR %s\n", gnutar_listdir);
    } else {
	dbprintf("GNUTAR-LISTDIR is not set\n");
    }
    if (gnutar_directory) {
	dbprintf("DIRECTORY %s\n", gnutar_directory);
    }
    dbprintf("ONE-FILE-SYSTEM %s\n", gnutar_onefilesystem? "yes":"no");
    dbprintf("SPARSE %s\n", gnutar_sparse? "yes":"no");
    dbprintf("ATIME-PRESERVE %s\n", gnutar_atimepreserve? "yes":"no");
    dbprintf("CHECK-DEVICE %s\n", gnutar_checkdevice? "yes":"no");
    {
	amregex_t *rp;
	for (rp = re_table; rp->regex != NULL; rp++) {
	    switch (rp->typ) {
		case DMP_NORMAL : dbprintf("NORMAL %s\n", rp->regex); break;
		case DMP_IGNORE : dbprintf("IGNORE %s\n", rp->regex); break;
		case DMP_STRANGE: dbprintf("STRANGE %s\n", rp->regex); break;
		case DMP_SIZE   : dbprintf("SIZE %s\n", rp->regex); break;
		case DMP_ERROR  : dbprintf("ERROR %s\n", rp->regex); break;
	    }
	}
    }

    if (strcmp(command, "support") == 0) {
	amgtar_support(&argument);
    } else if (strcmp(command, "selfcheck") == 0) {
	amgtar_selfcheck(&argument);
    } else if (strcmp(command, "estimate") == 0) {
	amgtar_estimate(&argument);
    } else if (strcmp(command, "backup") == 0) {
	amgtar_backup(&argument);
    } else if (strcmp(command, "restore") == 0) {
	amgtar_restore(&argument);
    } else if (strcmp(command, "validate") == 0) {
	amgtar_validate(&argument);
    } else {
	dbprintf("Unknown command `%s'.\n", command);
	fprintf(stderr, "Unknown command `%s'.\n", command);
	exit (1);
    }
    return 0;
}

static void
amgtar_support(
    application_argument_t *argument)
{
    (void)argument;
    fprintf(stdout, "CONFIG YES\n");
    fprintf(stdout, "HOST YES\n");
    fprintf(stdout, "DISK YES\n");
    fprintf(stdout, "MAX-LEVEL 9\n");
    fprintf(stdout, "INDEX-LINE YES\n");
    fprintf(stdout, "INDEX-XML NO\n");
    fprintf(stdout, "MESSAGE-LINE YES\n");
    fprintf(stdout, "MESSAGE-XML NO\n");
    fprintf(stdout, "RECORD YES\n");
    fprintf(stdout, "INCLUDE-FILE YES\n");
    fprintf(stdout, "INCLUDE-LIST YES\n");
    fprintf(stdout, "INCLUDE-OPTIONAL YES\n");
    fprintf(stdout, "EXCLUDE-FILE YES\n");
    fprintf(stdout, "EXCLUDE-LIST YES\n");
    fprintf(stdout, "EXCLUDE-OPTIONAL YES\n");
    fprintf(stdout, "COLLECTION NO\n");
    fprintf(stdout, "MULTI-ESTIMATE YES\n");
    fprintf(stdout, "CALCSIZE YES\n");
}

static void
amgtar_selfcheck(
    application_argument_t *argument)
{
    amgtar_build_exinclude(&argument->dle, 1, NULL, NULL, NULL, NULL);

    if (gnutar_path) {
	check_file(gnutar_path, X_OK);
    } else {
	printf(_("ERROR [GNUTAR program not available]\n"));
    }

    set_root_privs(1);
    if (gnutar_listdir && strlen(gnutar_listdir) == 0)
	gnutar_listdir = NULL;
    if (gnutar_listdir) {
	check_dir(gnutar_listdir, R_OK|W_OK);
    } else {
	printf(_("ERROR [No GNUTAR-LISTDIR]\n"));
    }

    fprintf(stdout, "OK %s\n", argument->dle.disk);
    if (gnutar_directory) {
	check_dir(gnutar_directory, R_OK);
    } else {
	check_dir(argument->dle.device, R_OK);
    }
    set_root_privs(0);
}

static void
amgtar_estimate(
    application_argument_t *argument)
{
    char  *incrname = NULL;
    char **my_argv = NULL;
    char  *cmd = NULL;
    int    nullfd = -1;
    int    pipefd = -1;
    FILE  *dumpout = NULL;
    off_t  size = -1;
    char   line[32768];
    char  *errmsg = NULL;
    char  *qerrmsg = NULL;
    char  *qdisk;
    amwait_t wait_status;
    int tarpid;
    amregex_t *rp;
    times_t start_time;
    int     level;
    GSList *levels;

    qdisk = quote_string(argument->dle.disk);

    if (argument->calcsize) {
	char *dirname;
	char *file_exclude;
	char *file_include;
	int   nb_exclude;
	int   nb_include;

	if (gnutar_directory) {
	    dirname = gnutar_directory;
	} else {
	    dirname = amname_to_dirname(argument->dle.device);
	}
	amgtar_build_exinclude(&argument->dle, 1,
			       &nb_exclude, &file_exclude,
			       &nb_include, &file_include);

	run_calcsize(argument->config, "GNUTAR", argument->dle.disk, dirname,
		     argument->level, file_exclude, file_include);
	return;
    }

    if (!gnutar_path) {
	errmsg = vstrallocf(_("GNUTAR-PATH not defined"));
	goto common_error;
    }

    if (!gnutar_listdir) {
	errmsg = vstrallocf(_("GNUTAR-LISTDIR not defined"));
	goto common_error;
    }

    for (levels = argument->level; levels != NULL; levels = levels->next) {
	level = GPOINTER_TO_INT(levels->data);
	incrname = amgtar_get_incrname(argument, level);
	cmd = stralloc(gnutar_path);
	my_argv = amgtar_build_argv(argument, incrname, CMD_ESTIMATE);

	start_time = curclock();

	if ((nullfd = open("/dev/null", O_RDWR)) == -1) {
	    errmsg = vstrallocf(_("Cannot access /dev/null : %s"),
				strerror(errno));
	    goto common_exit;
	}

	tarpid = pipespawnv(cmd, STDERR_PIPE, 1,
			    &nullfd, &nullfd, &pipefd, my_argv);

	dumpout = fdopen(pipefd,"r");
	if (!dumpout) {
	    error(_("Can't fdopen: %s"), strerror(errno));
	    /*NOTREACHED*/
	}

	size = (off_t)-1;
	while (size < 0 && (fgets(line, sizeof(line), dumpout) != NULL)) {
	    if (line[strlen(line)-1] == '\n') /* remove trailling \n */
		line[strlen(line)-1] = '\0';
	    if (line[0] == '\0')
		continue;
	    dbprintf("%s\n", line);
	    /* check for size match */
	    /*@ignore@*/
	    for(rp = re_table; rp->regex != NULL; rp++) {
		if(match(rp->regex, line)) {
		    if (rp->typ == DMP_SIZE) {
			size = ((the_num(line, rp->field)*rp->scale+1023.0)/1024.0);
			if(size < 0.0)
			    size = 1.0;             /* found on NeXT -- sigh */
		    }
		    break;
		}
	    }
	    /*@end@*/
	}

	while (fgets(line, sizeof(line), dumpout) != NULL) {
	    dbprintf("%s", line);
	}

	dbprintf(".....\n");
	dbprintf(_("estimate time for %s level %d: %s\n"),
		 qdisk,
		 level,
		 walltime_str(timessub(curclock(), start_time)));
	if(size == (off_t)-1) {
	    errmsg = vstrallocf(_("no size line match in %s output"),
				my_argv[0]);
	    dbprintf(_("%s for %s\n"), errmsg, qdisk);
	    dbprintf(".....\n");
	} else if(size == (off_t)0 && argument->level == 0) {
	    dbprintf(_("possible %s problem -- is \"%s\" really empty?\n"),
		      my_argv[0], argument->dle.disk);
	    dbprintf(".....\n");
	}
	dbprintf(_("estimate size for %s level %d: %lld KB\n"),
		 qdisk,
		 level,
		 (long long)size);

	kill(-tarpid, SIGTERM);

	dbprintf(_("waiting for %s \"%s\" child\n"), my_argv[0], qdisk);
	waitpid(tarpid, &wait_status, 0);
	if (WIFSIGNALED(wait_status)) {
	    errmsg = vstrallocf(_("%s terminated with signal %d: see %s"),
				cmd, WTERMSIG(wait_status), dbfn());
	} else if (WIFEXITED(wait_status)) {
	    if (exit_value[WEXITSTATUS(wait_status)] == 1) {
		errmsg = vstrallocf(_("%s exited with status %d: see %s"),
				    cmd, WEXITSTATUS(wait_status), dbfn());
	    } else {
		/* Normal exit */
	    }
	} else {
	    errmsg = vstrallocf(_("%s got bad exit: see %s"),
				cmd, dbfn());
	}
	dbprintf(_("after %s %s wait\n"), my_argv[0], qdisk);

common_exit:
	if (errmsg) {
	    dbprintf("%s", errmsg);
	    fprintf(stdout, "ERROR %s\n", errmsg);
	}

	if (incrname) {
	    unlink(incrname);
	}
	amfree(my_argv);
	amfree(cmd);

	aclose(nullfd);
	afclose(dumpout);

	fprintf(stdout, "%d %lld 1\n", level, (long long)size);
    }
    amfree(qdisk);
    return;

common_error:
    qerrmsg = quote_string(errmsg);
    amfree(qdisk);
    dbprintf("%s", errmsg);
    fprintf(stdout, "ERROR %s\n", qerrmsg);
    amfree(errmsg);
    amfree(qerrmsg);
    return;
}

static void
amgtar_backup(
    application_argument_t *argument)
{
    int dumpin;
    char *cmd = NULL;
    char *qdisk;
    char *incrname;
    char line[32768];
    amregex_t *rp;
    off_t dump_size = -1;
    char *type;
    char startchr;

    int dataf = 1;
    int mesgf = 3;
    int indexf = 4;
    int outf;
    FILE *mesgstream;
    FILE *indexstream = NULL;
    FILE *outstream;
    char *errmsg = NULL;
    amwait_t wait_status;

    char **my_argv;
    int tarpid;

    if (!gnutar_path) {
	error(_("GNUTAR-PATH not defined"));
    }
    if (!gnutar_listdir) {
	error(_("GNUTAR-LISTDIR not defined"));
    }

    qdisk = quote_string(argument->dle.disk);

    incrname = amgtar_get_incrname(argument,
				   GPOINTER_TO_INT(argument->level->data));
    cmd = stralloc(gnutar_path);
    my_argv = amgtar_build_argv(argument, incrname, CMD_BACKUP);

    tarpid = pipespawnv(cmd, STDIN_PIPE|STDERR_PIPE, 1,
			&dumpin, &dataf, &outf, my_argv);
    /* close the write ends of the pipes */

    aclose(dumpin);
    aclose(dataf);
    if (argument->dle.create_index) {
	indexstream = fdopen(indexf, "w");
	if (!indexstream) {
	    error(_("error indexstream(%d): %s\n"), indexf, strerror(errno));
	}
    }
    mesgstream = fdopen(mesgf, "w");
    if (!mesgstream) {
	error(_("error mesgstream(%d): %s\n"), mesgf, strerror(errno));
    }
    outstream = fdopen(outf, "r");
    if (!outstream) {
	error(_("error outstream(%d): %s\n"), outf, strerror(errno));
    }

    while (fgets(line, sizeof(line), outstream) != NULL) {
	if (line[strlen(line)-1] == '\n') /* remove trailling \n */
	    line[strlen(line)-1] = '\0';
	if (*line == '.' && *(line+1) == '/') { /* filename */
	    if (argument->dle.create_index) {
		fprintf(indexstream, "%s\n", &line[1]); /* remove . */
	    }
	} else { /* message */
	    for(rp = re_table; rp->regex != NULL; rp++) {
		if(match(rp->regex, line)) {
		    break;
		}
	    }
	    if(rp->typ == DMP_SIZE) {
		dump_size = (long)((the_num(line, rp->field)* rp->scale+1023.0)/1024.0);
	    }
	    switch(rp->typ) {
	    case DMP_NORMAL:
		type = "normal";
		startchr = '|';
		break;
	    case DMP_IGNORE:
		continue;
	    case DMP_STRANGE:
		type = "strange";
		startchr = '?';
		break;
	    case DMP_SIZE:
		type = "size";
		startchr = '|';
		break;
	    case DMP_ERROR:
		type = "error";
		startchr = '?';
		break;
	    default:
		type = "unknown";
		startchr = '!';
		break;
	    }
	    dbprintf("%3d: %7s(%c): %s\n", rp->srcline, type, startchr, line);
	    fprintf(mesgstream,"%c %s\n", startchr, line);
        }
    }

    waitpid(tarpid, &wait_status, 0);
    if (WIFSIGNALED(wait_status)) {
	errmsg = vstrallocf(_("%s terminated with signal %d: see %s"),
			    cmd, WTERMSIG(wait_status), dbfn());
    } else if (WIFEXITED(wait_status)) {
	if (exit_value[WEXITSTATUS(wait_status)] == 1) {
	    errmsg = vstrallocf(_("%s exited with status %d: see %s"),
				cmd, WEXITSTATUS(wait_status), dbfn());
	} else {
	    /* Normal exit */
	}
    } else {
	errmsg = vstrallocf(_("%s got bad exit: see %s"),
			    cmd, dbfn());
    }
    dbprintf(_("after %s %s wait\n"), my_argv[0], qdisk);
    dbprintf(_("amgtar: %s: pid %ld\n"), cmd, (long)tarpid);
    if (errmsg) {
	dbprintf("%s", errmsg);
	g_fprintf(mesgstream, "sendbackup: error [%s]\n", errmsg);
    }

    if (!errmsg && incrname && strlen(incrname) > 4) {
	char *nodotnew;
	nodotnew = stralloc(incrname);
	nodotnew[strlen(nodotnew)-4] = '\0';
	if (rename(incrname, nodotnew)) {
	    dbprintf(_("%s: warning [renaming %s to %s: %s]\n"),
		     get_pname(), incrname, nodotnew, strerror(errno));
	    g_fprintf(mesgstream, _("? warning [renaming %s to %s: %s]\n"),
		      incrname, nodotnew, strerror(errno));
	}
	amfree(nodotnew);
    }

    dbprintf("sendbackup: size %lld\n", (long long)dump_size);
    fprintf(mesgstream, "sendbackup: size %lld\n", (long long)dump_size);
    dbprintf("sendbackup: end\n");
    fprintf(mesgstream, "sendbackup: end\n");

    if (argument->dle.create_index)
	fclose(indexstream);

    fclose(mesgstream);

    amfree(incrname);
    amfree(qdisk);
    amfree(cmd);
}

static void
amgtar_restore(
    application_argument_t *argument)
{
    char  *cmd;
    char **my_argv;
    char **env;
    int    i, j;
    char  *e;

    if (!gnutar_path) {
	error(_("GNUTAR-PATH not defined"));
    }

    cmd = stralloc(gnutar_path);
    my_argv = alloc(SIZEOF(char *) * (6 + argument->argc));
    i = 0;
    my_argv[i++] = stralloc(gnutar_path);
    my_argv[i++] = stralloc("--numeric-owner");
    my_argv[i++] = stralloc("-xpGvf");
    my_argv[i++] = stralloc("-");

    for (j=1; j< argument->argc; j++) {
	my_argv[i++] = stralloc(argument->argv[j]);
    }
    my_argv[i++] = NULL;

    env = safe_env();
    become_root();
    execve(cmd, my_argv, env);
    e = strerror(errno);
    error(_("error [exec %s: %s]"), cmd, e);
}

static void
amgtar_validate(
    application_argument_t *argument G_GNUC_UNUSED)
{
    char  *cmd;
    char **my_argv;
    char **env;
    int    i;
    char  *e;

    if (!gnutar_path) {
	error(_("GNUTAR-PATH not defined"));
    }

    cmd = stralloc(gnutar_path);
    my_argv = alloc(SIZEOF(char *) * 4);
    i = 0;
    my_argv[i++] = stralloc(gnutar_path);
    my_argv[i++] = stralloc("-tf");
    my_argv[i++] = stralloc("-");
    my_argv[i++] = NULL;

    env = safe_env();
    execve(cmd, my_argv, env);
    e = strerror(errno);
    error(_("error [exec %s: %s]"), cmd, e);
}

static void
amgtar_build_exinclude(
    dle_t  *dle,
    int     verbose,
    int    *nb_exclude,
    char  **file_exclude,
    int    *nb_include,
    char  **file_include)
{
    int n_exclude = 0;
    int n_include = 0;
    char *exclude = NULL;
    char *include = NULL;

    if (dle->exclude_file) n_exclude += dle->exclude_file->nb_element;
    if (dle->exclude_list) n_exclude += dle->exclude_list->nb_element;
    if (dle->include_file) n_include += dle->include_file->nb_element;
    if (dle->include_list) n_include += dle->include_list->nb_element;

    if (n_exclude > 0) exclude = build_exclude(dle, verbose);
    if (n_include > 0) include = build_include(dle, verbose);

    if (nb_exclude)
	*nb_exclude = n_exclude;
    if (file_exclude)
	*file_exclude = exclude;
    else
	amfree(exclude);

    if (nb_include)
	*nb_include = n_include;
    if (file_include)
	*file_include = include;
    else
	amfree(include);
}

static char *
amgtar_get_incrname(
    application_argument_t *argument,
    int                     level)
{
    char *basename = NULL;
    char *incrname = NULL;
    int   infd, outfd;
    ssize_t   nb;
    char *inputname = NULL;
    char *errmsg = NULL;
    char *buf;

    if (gnutar_listdir) {
	char number[NUM_STR_SIZE];
	int baselevel;
	char *sdisk = sanitise_filename(argument->dle.disk);

	basename = vstralloc(gnutar_listdir,
			     "/",
			     argument->host,
			     sdisk,
			     NULL);
	amfree(sdisk);

	snprintf(number, SIZEOF(number), "%d", level);
	incrname = vstralloc(basename, "_", number, ".new", NULL);
	unlink(incrname);

	/*
	 * Open the listed incremental file from the previous level.  Search
	 * backward until one is found.  If none are found (which will also
	 * be true for a level 0), arrange to read from /dev/null.
	 */
	baselevel = level;
	infd = -1;
	while (infd == -1) {
	    if (--baselevel >= 0) {
		snprintf(number, SIZEOF(number), "%d", baselevel);
		inputname = newvstralloc(inputname,
					 basename, "_", number, NULL);
	    } else {
		inputname = newstralloc(inputname, "/dev/null");
	    }
	    if ((infd = open(inputname, O_RDONLY)) == -1) {

		errmsg = vstrallocf(_("amgtar: error opening %s: %s"),
				     inputname, strerror(errno));
		dbprintf("%s\n", errmsg);
		if (baselevel < 0) {
		    return NULL;
		}
		amfree(errmsg);
	    }
	}

	/*
	 * Copy the previous listed incremental file to the new one.
	 */
	if ((outfd = open(incrname, O_WRONLY|O_CREAT, 0600)) == -1) {
	    errmsg = vstrallocf(_("opening %s: %s"),
			         incrname, strerror(errno));
	    dbprintf("%s\n", errmsg);
	    return NULL;
	}

	while ((nb = read(infd, &buf, SIZEOF(buf))) > 0) {
	    if (full_write(outfd, &buf, (size_t)nb) < (size_t)nb) {
		errmsg = vstrallocf(_("writing to %s: %s"),
				     incrname, strerror(errno));
		dbprintf("%s\n", errmsg);
		return NULL;
	    }
	}

	if (nb < 0) {
	    errmsg = vstrallocf(_("reading from %s: %s"),
			         inputname, strerror(errno));
	    dbprintf("%s\n", errmsg);
	    return NULL;
	}

	if (close(infd) != 0) {
	    errmsg = vstrallocf(_("closing %s: %s"),
			         inputname, strerror(errno));
	    dbprintf("%s\n", errmsg);
	    return NULL;
	}
	if (close(outfd) != 0) {
	    errmsg = vstrallocf(_("closing %s: %s"),
			         incrname, strerror(errno));
	    dbprintf("%s\n", errmsg);
	    return NULL;
	}

	amfree(inputname);
	amfree(basename);
    }
    return incrname;
}

char **amgtar_build_argv(
    application_argument_t *argument,
    char *incrname,
    int   command)
{
    int    i;
    int    nb_exclude;
    int    nb_include;
    char  *file_exclude;
    char  *file_include;
    char  *dirname;
    char   tmppath[PATH_MAX];
    char **my_argv;

    amgtar_build_exinclude(&argument->dle, 1,
			   &nb_exclude, &file_exclude,
			   &nb_include, &file_include);

    if (gnutar_directory) {
	dirname = gnutar_directory;
    } else {
	dirname = amname_to_dirname(argument->dle.device);
    }

    my_argv = alloc(SIZEOF(char *) * 23);
    i = 0;

    my_argv[i++] = gnutar_path;

    my_argv[i++] = "--create";
    if (command == CMD_BACKUP && argument->dle.create_index)
	my_argv[i++] = "--verbose";
    my_argv[i++] = "--file";
    if (command == CMD_ESTIMATE) {
	my_argv[i++] = "/dev/null";
    } else {
	my_argv[i++] = "-";
    }
    my_argv[i++] = "--directory";
    canonicalize_pathname(dirname, tmppath);
    my_argv[i++] = stralloc(tmppath);
    if (gnutar_onefilesystem)
	my_argv[i++] = "--one-file-system";
    if (gnutar_atimepreserve)
	my_argv[i++] = "--atime-preserve=system";
    if (!gnutar_checkdevice)
	my_argv[i++] = "--no-check-device";
    my_argv[i++] = "--listed-incremental";
    my_argv[i++] = incrname;
    if (gnutar_sparse)
	my_argv[i++] = "--sparse";
    if (argument->tar_blocksize) {
	my_argv[i++] = "--blocking-factor";
	my_argv[i++] = argument->tar_blocksize;
    }
    my_argv[i++] = "--ignore-failed-read";
    my_argv[i++] = "--totals";

    if(file_exclude) {
	my_argv[i++] = "--exclude-from";
	my_argv[i++] = file_exclude;
    }

    if(file_include) {
	my_argv[i++] = "--files-from";
	my_argv[i++] = file_include;
    }
    else {
	my_argv[i++] = ".";
    }
    my_argv[i++] = NULL;

    return(my_argv);
}

