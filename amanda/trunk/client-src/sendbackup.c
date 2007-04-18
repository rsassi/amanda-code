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
 * $Id: sendbackup.c,v 1.88 2006/07/25 18:27:56 martinea Exp $
 *
 * common code for the sendbackup-* programs.
 */

#include "amanda.h"
#include "sendbackup.h"
#include "clock.h"
#include "pipespawn.h"
#include "amfeatures.h"
#include "amandad.h"
#include "arglist.h"
#include "getfsent.h"
#include "version.h"
#include "conffile.h"

#define sendbackup_debug(i,x) do {	\
	if ((i) <= debug_sendbackup) {	\
	    dbprintf(x);		\
	}				\
} while (0)

#define TIMEOUT 30

pid_t comppid = (pid_t)-1;
pid_t dumppid = (pid_t)-1;
pid_t tarpid = (pid_t)-1;
pid_t encpid = (pid_t)-1;
pid_t indexpid = (pid_t)-1;
char *errorstr = NULL;

int datafd;
int mesgfd;
int indexfd;

option_t *options;
g_option_t *g_options = NULL;

long dump_size = -1;

backup_program_t *program = NULL;

static am_feature_t *our_features = NULL;
static char *our_feature_string = NULL;
static char *amandad_auth = NULL;

/* local functions */
int main(int argc, char **argv);
char *optionstr(option_t *options);
char *childstr(pid_t pid);
int check_status(pid_t pid, amwait_t w);

pid_t pipefork(void (*func)(void), char *fname, int *stdinfd,
		int stdoutfd, int stderrfd);
void parse_backup_messages(int mesgin);
static void process_dumpline(char *str);
static void save_fd(int *, int);
void backup_api_info_tapeheader(int mesgfd, char *prog, option_t *options);

double the_num(char *str, int pos);


char *
optionstr(
    option_t *	options)
{
    static char *optstr = NULL;
    char *compress_opt;
    char *encrypt_opt;
    char *decrypt_opt;
    char *record_opt = "";
    char *index_opt = "";
    char *auth_opt;
    char *exclude_file_opt;
    char *exclude_list_opt;
    char *exc = NULL;
    sle_t *excl;

    if(options->compress == COMP_BEST)
	compress_opt = stralloc("compress-best;");
    else if(options->compress == COMP_FAST)
	compress_opt = stralloc("compress-fast;");
    else if(options->compress == COMP_SERVER_BEST)
	compress_opt = stralloc("srvcomp-best;");
    else if(options->compress == COMP_SERVER_FAST)
	compress_opt = stralloc("srvcomp-fast;");
    else if(options->compress == COMP_SERVER_CUST)
	compress_opt = vstralloc("srvcomp-cust=", options->srvcompprog, ";", NULL);
    else if(options->compress == COMP_CUST)
	compress_opt = vstralloc("comp-cust=", options->clntcompprog, ";", NULL);
    else
	compress_opt = stralloc("");
    
    if(options->encrypt == ENCRYPT_CUST) {
      encrypt_opt = vstralloc("encrypt-cust=", options->clnt_encrypt, ";", NULL);
      if (options->clnt_decrypt_opt)
	decrypt_opt = vstralloc("client-decrypt-option=", options->clnt_decrypt_opt, ";", NULL);
      else
	decrypt_opt = stralloc("");
    }
    else if(options->encrypt == ENCRYPT_SERV_CUST) {
      encrypt_opt = vstralloc("encrypt-serv-cust=", options->srv_encrypt, ";", NULL);
      if(options->srv_decrypt_opt)
	decrypt_opt = vstralloc("server-decrypt-option=", options->srv_decrypt_opt, ";", NULL);
      else
	decrypt_opt = stralloc("");
    }
    else {
	encrypt_opt = stralloc("");
	decrypt_opt = stralloc("");
    }

    if(options->no_record) record_opt = "no-record;";
    if(options->auth) auth_opt = vstralloc("auth=", options->auth, ";", NULL);
	else auth_opt = stralloc("");
    if(options->createindex) index_opt = "index;";

    exclude_file_opt = stralloc("");
    if(options->exclude_file) {
	for(excl = options->exclude_file->first; excl != NULL; excl=excl->next){
	    exc = newvstralloc(exc, "exclude-file=", excl->name, ";", NULL);
	    strappend(exclude_file_opt, exc);
	}
    }
    exclude_list_opt = stralloc("");
    if(options->exclude_list) {
	for(excl = options->exclude_list->first; excl != NULL; excl=excl->next){
	    exc = newvstralloc(exc, "exclude-list=", excl->name, ";", NULL);
	    strappend(exclude_list_opt, exc);
	}
    }
    amfree(exc);
    optstr = newvstralloc(optstr,
			  compress_opt,
			  encrypt_opt,
			  decrypt_opt,
			  record_opt,
			  index_opt,
			  auth_opt,
			  exclude_file_opt,
			  exclude_list_opt,
			  NULL);
    amfree(compress_opt);
    amfree(encrypt_opt);
    amfree(decrypt_opt);
    amfree(auth_opt);
    amfree(exclude_file_opt);
    amfree(exclude_list_opt);
    return optstr;
}


int
main(
    int		argc,
    char **	argv)
{
    int interactive = 0;
    int level = 0;
    int mesgpipe[2];
    char *prog, *dumpdate, *stroptions;
    int program_is_backup_api;
    char *disk = NULL;
    char *qdisk = NULL;
    char *amdevice = NULL;
    char *qamdevice = NULL;
    char *line = NULL;
    char *err_extra = NULL;
    char *s;
    char *conffile;
    int i;
    int ch;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    FILE *toolin;
    int status;

    /* initialize */

    safe_fd(DATA_FD_OFFSET, DATA_FD_COUNT*2);

    safe_cd();

    set_pname("sendbackup");

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

    /* Don't die when interrupt received */
    signal(SIGINT, SIG_IGN);

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    if(argc > 1 && strcmp(argv[1],"-t") == 0) {
	interactive = 1;
	argc--;
	argv++;
    } else {
	interactive = 0;
    }

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    dbopen(DBG_SUBDIR_CLIENT);
    startclock();
    dbprintf(("%s: version %s\n", get_pname(), version()));

    if(argc > 2 && strcmp(argv[1], "amandad") == 0) {
	amandad_auth = stralloc(argv[2]);
    }

    our_features = am_init_feature_set();
    our_feature_string = am_feature_to_string(our_features);

    conffile = vstralloc(CONFIG_DIR, "/", "amanda-client.conf", NULL);
    if (read_clientconf(conffile) > 0) {
	error("error reading conffile: %s", conffile);
	/*NOTREACHED*/
    }
    amfree(conffile);

    if(interactive) {
	/*
	 * In interactive (debug) mode, the backup data is sent to
	 * /dev/null and none of the network connections back to driver
	 * programs on the tape host are set up.  The index service is
	 * run and goes to stdout.
	 */
	fprintf(stderr, "%s: running in interactive test mode\n", get_pname());
	fflush(stderr);
    }

    prog = NULL;
    disk = NULL;
    qdisk = NULL;
    amdevice = NULL;
    dumpdate = NULL;
    stroptions = NULL;
    program_is_backup_api=0;

    for(; (line = agets(stdin)) != NULL; free(line)) {
	if (line[0] == '\0')
	    continue;
	if(interactive) {
	    fprintf(stderr, "%s> ", get_pname());
	    fflush(stderr);
	}
	if(strncmp_const(line, "OPTIONS ") == 0) {
	    g_options = parse_g_options(line+8, 1);
	    if(!g_options->hostname) {
		g_options->hostname = alloc(MAX_HOSTNAME_LENGTH+1);
		gethostname(g_options->hostname, MAX_HOSTNAME_LENGTH);
		g_options->hostname[MAX_HOSTNAME_LENGTH] = '\0';
	    }

	    if (g_options->config) {
		conffile = vstralloc(CONFIG_DIR, "/", g_options->config, "/",
				     "amanda-client.conf", NULL);
		if (read_clientconf(conffile) > 0) {
		    error("error reading conffile: %s", conffile);
		    /*NOTREACHED*/
		}
		amfree(conffile);

		dbrename(g_options->config, DBG_SUBDIR_CLIENT);
	    }
	    continue;
	}

	if (prog != NULL) {
	    err_extra = "multiple requests";
	    goto err;
	}

	dbprintf(("  sendbackup req: <%s>\n", line));
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the program name */
	if(ch == '\0') {
	    err_extra = "no program name";
	    goto err;				/* no program name */
	}
	prog = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	if(strcmp(prog,"BACKUP")==0) {
	    program_is_backup_api=1;
	    skip_whitespace(s, ch);		/* find dumper name */
	    if (ch == '\0') {
		goto err;			/* no program */
	    }
	    prog = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';
	}
	prog = stralloc(prog);

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    err_extra = "no disk name";
	    goto err;				/* no disk name */
	}

	amfree(disk);
	amfree(qdisk);
	qdisk = s - 1;
	ch = *qdisk;
	skip_quoted_string(s, ch);
	s[-1] = '\0';
	qdisk = stralloc(qdisk);
	disk = unquote_string(qdisk);

	skip_whitespace(s, ch);			/* find the device or level */
	if (ch == '\0') {
	    err_extra = "bad level";
	    goto err;
	}

	if(!isdigit((int)s[-1])) {
	    amfree(amdevice);
	    amfree(qamdevice);
	    qamdevice = s - 1;
	    ch = *qamdevice;
	    skip_quoted_string(s, ch);
	    s[-1] = '\0';
	    qamdevice = stralloc(qamdevice);
	    amdevice = unquote_string(qamdevice);
	    skip_whitespace(s, ch);		/* find level number */
	}
	else {
	    amdevice = stralloc(disk);
	    qamdevice = stralloc(qdisk);
	}
						/* find the level number */
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    err_extra = "bad level";
	    goto err;				/* bad level */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the dump date */
	if(ch == '\0') {
	    err_extra = "no dumpdate";
	    goto err;				/* no dumpdate */
	}
	amfree(dumpdate);
	dumpdate = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	dumpdate = stralloc(dumpdate);

	skip_whitespace(s, ch);			/* find the options keyword */
	if(ch == '\0') {
	    err_extra = "no options";
	    goto err;				/* no options */
	}
	if(strncmp_const_skip(s - 1, "OPTIONS ", s, ch) != 0) {
	    err_extra = "no OPTIONS keyword";
	    goto err;				/* no options */
	}
	skip_whitespace(s, ch);			/* find the options string */
	if(ch == '\0') {
	    err_extra = "bad options string";
	    goto err;				/* no options */
	}
	amfree(stroptions);
	stroptions = stralloc(s - 1);
    }
    amfree(line);

    if (prog       == NULL ||
	disk       == NULL ||
	amdevice   == NULL ||
	dumpdate   == NULL ||
	stroptions == NULL) {
	err_extra = "no valid sendbackup request";
	goto err;
    }
	
    dbprintf(("  parsed request as: program `%s'\n", prog));
    dbprintf(("                     disk `%s'\n", qdisk));
    dbprintf(("                     device `%s'\n", qamdevice));
    dbprintf(("                     level %d\n", level));
    dbprintf(("                     since %s\n", dumpdate));
    dbprintf(("                     options `%s'\n", stroptions));

    if(program_is_backup_api==1) {
	/* check that the backup_api exist */
    }
    else {
	for(i = 0; programs[i]; i++) {
	    if (strcmp(programs[i]->name, prog) == 0) {
		break;
	    }
	}
	if (programs[i] == NULL) {
	    dbprintf(("ERROR [%s: unknown program %s]\n", get_pname(), prog));
	    error("ERROR [%s: unknown program %s]", get_pname(), prog);
	    /*NOTREACHED*/
	}
	program = programs[i];
    }

    options = parse_options(stroptions, disk, amdevice, g_options->features, 0);

    if(!interactive) {
	datafd = DATA_FD_OFFSET + 0;
	mesgfd = DATA_FD_OFFSET + 2;
	indexfd = DATA_FD_OFFSET + 4;
    }
    if (!options->createindex)
	indexfd = -1;

    if(options->auth && amandad_auth) {
	if(strcasecmp(options->auth, amandad_auth) != 0) {
	    printf("ERROR [client configured for auth=%s while server requested '%s']\n",
		   amandad_auth, options->auth);
	    exit(-1);
	}
    }

    printf("CONNECT DATA %d MESG %d INDEX %d\n",
	   DATA_FD_OFFSET, DATA_FD_OFFSET+1,
	   indexfd == -1 ? -1 : DATA_FD_OFFSET+2);
    printf("OPTIONS ");
    if(am_has_feature(g_options->features, fe_rep_options_features)) {
	printf("features=%s;", our_feature_string);
    }
    if(am_has_feature(g_options->features, fe_rep_options_hostname)) {
	printf("hostname=%s;", g_options->hostname);
    }
    if(am_has_feature(g_options->features, fe_rep_options_sendbackup_options)) {
	printf("%s", optionstr(options));
    }
    printf("\n");
    fflush(stdout);
    if (freopen("/dev/null", "w", stdout) == NULL) {
	dbprintf(("%s: error redirecting stdout to /dev/null: %s\n",
	    debug_prefix_time(NULL), strerror(errno)));
        exit(1);
    }

    if(interactive) {
      if((datafd = open("/dev/null", O_RDWR)) < 0) {
	s = strerror(errno);
	error("ERROR [%s: open of /dev/null for debug data stream: %s]\n",
		  get_pname(), s);
	/*NOTREACHED*/
      }
      mesgfd = 2;
      indexfd = 1;
    }

    if(!interactive) {
      if(datafd == -1 || mesgfd == -1 || (options->createindex && indexfd == -1)) {
        dbclose();
        exit(1);
      }
    }

    if(program_is_backup_api==1) {
	pid_t backup_api_pid;
	int i, j;
	char *cmd=NULL;
	char *argvchild[20];
	char levelstr[20];
	int property_pipe[2];
	backup_support_option_t *bsu;

	if (pipe(property_pipe) < 0) {
	    error("Can't create pipe: %s",strerror(errno));
	    /*NOTREACHED*/
	}
	bsu = backup_support_option(prog, g_options, disk, amdevice);

	switch(backup_api_pid=fork()) {
	case 0:
	    aclose(property_pipe[1]);
	    if(dup2(property_pipe[0], 0) == -1) {
		error("Can't dup2: %s",strerror(errno));
		/*NOTREACHED*/
	    }
	    if(dup2(datafd, 1) == -1) {
		error("Can't dup2: %s",strerror(errno));
		/*NOTREACHED*/
	    }
	    if(dup2(mesgfd, 2) == -1) {
		error("Can't dup2: %s",strerror(errno));
		/*NOTREACHED*/
	    }
	    if(indexfd != 0) {
		if(dup2(indexfd, 3) == -1) {
		    error("Can't dup2: %s",strerror(errno));
		    /*NOTREACHED*/
		}
		fcntl(indexfd, F_SETFD, 0);
		fcntl(3, F_SETFD, 0);
	    }
	    cmd = vstralloc(DUMPER_DIR, "/", prog, NULL);
	    i=0;
	    argvchild[i++] = prog;
	    argvchild[i++] = "backup";
	    if (bsu->message_line == 1) {
		argvchild[i++] = "--message";
		argvchild[i++] = "line";
	    }
	    if (g_options->config && bsu->config == 1) {
		argvchild[i++] = "--config";
		argvchild[i++] = g_options->config;
	    }
	    if (g_options->hostname && bsu->host == 1) {
		argvchild[i++] = "--host";
		argvchild[i++] = g_options->hostname;
	    }
	    if (disk && bsu->disk == 1) {
		argvchild[i++] = "--disk";
		argvchild[i++] = disk;
	    }
	    argvchild[i++] = "--device";
	    argvchild[i++] = amdevice;
	    if (level <= bsu->max_level) {
		argvchild[i++] = "--level";
		snprintf(levelstr,19,"%d",level);
		argvchild[i++] = levelstr;
	    }
	    if (indexfd != 0 && bsu->index_line == 1) {
		argvchild[i++] = "--index";
		argvchild[i++] = "line";
	    }
	    if (!options->no_record && bsu->record == 1) {
		argvchild[i++] = "--record";
	    }
	    argvchild[i] = NULL;
	    dbprintf(("%s: running \"%s", get_pname(), cmd));
	    for(j=1;j<i;j++) dbprintf((" %s",argvchild[j]));
	    dbprintf(("\"\n"));
	    backup_api_info_tapeheader(mesgfd, prog, options);
	    execve(cmd, argvchild, safe_env());
	    exit(1);
	    break;
 
	default:
	    aclose(property_pipe[0]);
	    toolin = fdopen(property_pipe[1],"w");
	    if (!toolin) {
		error("Can't fdopen: %s", strerror(errno));
		/*NOTREACHED*/
	    }
	    output_tool_property(toolin, options);
	    fflush(toolin);
	    fclose(toolin);
	    break;
	case -1:
	    error("%s: fork returned: %s", get_pname(), strerror(errno));
	}
	amfree(bsu);
	if (waitpid(backup_api_pid, &status, 0) < 0) {
	    if (!WIFEXITED(status)) {
		dbprintf(("Tool exited with signal %d", WTERMSIG(status)));
	    } else if (WEXITSTATUS(status) != 0) {
		dbprintf(("Tool exited with status %d", WEXITSTATUS(status)));
	    } else {
		dbprintf(("waitpid returned negative value"));
	    }
	}
     }
    else {
	if(!interactive) {
	    /* redirect stderr */
	    if(dup2(mesgfd, 2) == -1) {
		dbprintf(("%s: error redirecting stderr to fd %d: %s\n",
			  debug_prefix_time(NULL), mesgfd, strerror(errno)));
		dbclose();
		exit(1);
	    }
	}
 
	if(pipe(mesgpipe) == -1) {
	    s = strerror(errno);
	    dbprintf(("error [opening mesg pipe: %s]\n", s));
	    error("error [opening mesg pipe: %s]", s);
	}

	program->start_backup(g_options->hostname, disk, amdevice, level,
			      dumpdate, datafd, mesgpipe[1], indexfd);
	dbprintf(("%s: started backup\n", debug_prefix_time(NULL)));
	parse_backup_messages(mesgpipe[0]);
	dbprintf(("%s: parsed backup messages\n", debug_prefix_time(NULL)));
    }

    amfree(prog);
    amfree(disk);
    amfree(qdisk);
    amfree(amdevice);
    amfree(qamdevice);
    amfree(dumpdate);
    amfree(stroptions);
    amfree(our_feature_string);
    am_release_feature_set(our_features);
    our_features = NULL;
    am_release_feature_set(g_options->features);
    g_options->features = NULL;
    amfree(g_options->hostname);
    amfree(g_options->str);
    amfree(g_options);

    dbclose();

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;

 err:
    printf("FORMAT ERROR IN REQUEST PACKET\n");
    dbprintf(("%s: REQ packet is bogus%s%s\n",
	      debug_prefix_time(NULL),
	      err_extra ? ": " : "",
	      err_extra ? err_extra : ""));
    dbclose();
    return 1;
}


/*
 * Returns a string for a child process.  Checks the saved dump and
 * compress pids to see which it is.
 */

char *
childstr(
    pid_t pid)
{
    if(pid == dumppid) return program->backup_name;
    if(pid == comppid) return "compress";
    if(pid == encpid) return "encrypt";
    if(pid == indexpid) return "index";
    return "unknown";
}


/*
 * Determine if the child return status really indicates an error.
 * If so, add the error message to the error string; more than one
 * child can have an error.
 */

int
check_status(
    pid_t	pid,
    amwait_t	w)
{
    char *thiserr = NULL;
    char *str, *strX;
    int ret, sig, rc;
    char number[NUM_STR_SIZE];
    char numberpid[NUM_STR_SIZE];

    str = childstr(pid);

    if(WIFSIGNALED(w)) {
	ret = 0;
	rc = sig = WTERMSIG(w);
    } else {
	sig = 0;
	rc = ret = WEXITSTATUS(w);
    }

    if(pid == indexpid) {
	/*
	 * Treat an index failure (other than signal) as a "STRANGE"
	 * rather than an error so the dump goes ahead and gets processed
	 * but the failure is noted.
	 */
	if(ret != 0) {
	    fprintf(stderr, "? index %s returned %d\n", str, ret);
	    rc = 0;
	}
	indexpid = -1;
	strX = "index ";
    } else if(pid == comppid) {
	/*
	 * compress returns 2 sometimes, but it is ok.
	 */
#ifndef HAVE_GZIP
	if(ret == 2) {
	    rc = 0;
	}
#endif
	comppid = -1;
	strX = "compress ";
    } else if(pid == dumppid && tarpid == -1) {
        /*
	 * Ultrix dump returns 1 sometimes, but it is ok.
	 */
#ifdef DUMP_RETURNS_1
        if(ret == 1) {
	    rc = 0;
	}
#endif
	dumppid = -1;
	strX = "dump ";
    } else if(pid == tarpid) {
	if (ret == 1) {
	    rc = 0;
	}
	/*
	 * tar bitches about active filesystems, but we do not care.
	 */
#ifdef IGNORE_TAR_ERRORS
        if(ret == 2) {
	    rc = 0;
	}
#endif
	dumppid = tarpid = -1;
	strX = "dump ";
    } else {
	strX = "unknown ";
    }

    if(rc == 0) {
	return 0;				/* normal exit */
    }

    snprintf(numberpid, SIZEOF(number), "%d", (int)pid);
    if(ret == 0) {
	snprintf(number, SIZEOF(number), "%d", sig);
	thiserr = vstralloc(strX, "(", numberpid, ") ", str, " got signal ", number, NULL);
    } else {
	snprintf(number, SIZEOF(number), "%d", ret);
	thiserr = vstralloc(strX, "(", numberpid, ") ", str, " returned ", number, NULL);
    }

    if(errorstr) {
	strappend(errorstr, ", ");
	strappend(errorstr, thiserr);
	amfree(thiserr);
    } else {
	errorstr = thiserr;
	thiserr = NULL;
    }
    return 1;
}


/*
 *Send header info to the message file.
 */
void
info_tapeheader(void)
{
    fprintf(stderr, "%s: info BACKUP=%s\n", get_pname(), program->backup_name);

    fprintf(stderr, "%s: info RECOVER_CMD=", get_pname());
    if (options->compress == COMP_FAST || options->compress == COMP_BEST)
	fprintf(stderr, "%s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT
#else
		""
#endif
		);

    fprintf(stderr, "%s -xpGf - ...\n", program->restore_name);

    if (options->compress == COMP_FAST || options->compress == COMP_BEST)
	fprintf(stderr, "%s: info COMPRESS_SUFFIX=%s\n",
			get_pname(), COMPRESS_SUFFIX);

    fprintf(stderr, "%s: info end\n", get_pname());
}

void
backup_api_info_tapeheader(
    int       mesgfd,
    char     *prog,
    option_t *options)
{
    char line[1024];

    snprintf(line, 1024, "%s: info BACKUP=DUMPER\n", get_pname());
    if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	return;
    }

    snprintf(line, 1024, "%s: info DUMPER=%s\n", get_pname(), prog);
    if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	return;
    }

    snprintf(line, 1024, "%s: info RECOVER_CMD=", get_pname());
    if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	return;
    }

    if (options->compress) {
	snprintf(line, 1024, "%s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		 UNCOMPRESS_OPT
#else
		 ""
#endif
		 );
	if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	    dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	    return;
	}
    }
    snprintf(line, 1024, "%s -f... -\n", prog);
    if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	return;
    }

    if (options->compress) {
	snprintf(line, 1024, "%s: info COMPRESS_SUFFIX=%s\n",
		 get_pname(), COMPRESS_SUFFIX);
	if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	    dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	    return;
	}
    }

    snprintf(line, 1024, "%s: info end\n", get_pname());
    if (fullwrite(mesgfd, line, strlen(line)) != (ssize_t)strlen(line)) {
	dbprintf(("error writing to mesgfd socket: %s", strerror(errno)));
	return;
    }
}

pid_t
pipefork(
    void	(*func)(void),
    char *	fname,
    int *	stdinfd,
    int		stdoutfd,
    int		stderrfd)
{
    int inpipe[2];
    pid_t pid;

    dbprintf(("%s: forking function %s in pipeline\n",
	debug_prefix_time(NULL), fname));

    if(pipe(inpipe) == -1) {
	error("error [open pipe to %s: %s]", fname, strerror(errno));
	/*NOTREACHED*/
    }

    switch(pid = fork()) {
    case -1:
	error("error [fork %s: %s]", fname, strerror(errno));
	/*NOTREACHED*/
    default:	/* parent process */
	aclose(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	aclose(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	    error("error [fork %s: dup2(%d, in): %s]",
		  fname, inpipe[0], strerror(errno));
	    /*NOTRACHED*/
	}
	if(dup2(stdoutfd, 1) == -1) {
	    error("error [fork %s: dup2(%d, out): %s]",
		  fname, stdoutfd, strerror(errno));
	    /*NOTRACHED*/
	}
	if(dup2(stderrfd, 2) == -1) {
	    error("error [fork %s: dup2(%d, err): %s]",
		  fname, stderrfd, strerror(errno));
	    /*NOTRACHED*/
	}

	func();
	exit(0);
	/*NOTREACHED*/
    }
    return pid;
}

void
parse_backup_messages(
    int		mesgin)
{
    int goterror;
    pid_t wpid;
    amwait_t retstat;
    char *line;

    goterror = 0;
    amfree(errorstr);

    for(; (line = areads(mesgin)) != NULL; free(line)) {
	process_dumpline(line);
    }

    if(errno) {
	error("error [read mesg pipe: %s]", strerror(errno));
	/*NOTREACHED*/
    }

    while((wpid = waitpid((pid_t)-1, &retstat, WNOHANG)) > 0) {
	if(check_status(wpid, retstat)) goterror = 1;
    }

    if (dumppid != -1) {
	sleep(5);
	while((wpid = waitpid((pid_t)-1, &retstat, WNOHANG)) > 0) {
	    if(check_status(wpid, retstat)) goterror = 1;
	}
    }
    if (dumppid != -1) {
	dbprintf(("%s: Sending SIGHUP to dump process %d\n",
		  debug_prefix_time(NULL), (int)dumppid));
	if(dumppid != -1) {
	    if(kill(dumppid, SIGHUP) == -1) {
		dbprintf(("%s: Can't send SIGHUP to %d: %s\n",
			  debug_prefix_time(NULL), (int)dumppid,
			  strerror(errno)));
	    }
	}
	sleep(5);
	while((wpid = waitpid((pid_t)-1, &retstat, WNOHANG)) > 0) {
	    if(check_status(wpid, retstat)) goterror = 1;
	}
    }
    if (dumppid != -1) {
	dbprintf(("%s: Sending SIGKILL to dump process %d\n",
		  debug_prefix_time(NULL), (int)dumppid));
	if(dumppid != -1) {
	    if(kill(dumppid, SIGKILL) == -1) {
		dbprintf(("%s: Can't send SIGKILL to %d: %s\n",
			  debug_prefix_time(NULL), (int)dumppid,
			  strerror(errno)));
	    }
	}
	sleep(5);
	while((wpid = waitpid((pid_t)-1, &retstat, WNOHANG)) > 0) {
	    if(check_status(wpid, retstat)) goterror = 1;
	}
    }

    if(errorstr) {
	error("error [%s]", errorstr);
	/*NOTREACHED*/
    } else if(dump_size == -1) {
	error("error [no backup size line]");
	/*NOTREACHED*/
    }

    program->end_backup(goterror);

    fprintf(stderr, "%s: size %ld\n", get_pname(), dump_size);
    fprintf(stderr, "%s: end\n", get_pname());
}


/*
 * Returns the value of the first integer in a string.
 */

double
the_num(
    char *	str,
    int         pos)
{
    char *num;
    int ch;
    double d;

    do {
	ch = *str++;
	while(ch && !isdigit(ch)) ch = *str++;
	if (pos == 1) break;
	pos--;
	while(ch && (isdigit(ch) || ch == '.')) ch = *str++;
    } while (ch);
    num = str - 1;
    while(isdigit(ch) || ch == '.') ch = *str++;
    str[-1] = '\0';
    d = atof(num);
    str[-1] = (char)ch;
    return d;
}


static void
process_dumpline(
    char *	str)
{
    amregex_t *rp;
    char *type;
    char startchr;

    for(rp = program->re_table; rp->regex != NULL; rp++) {
	if(match(rp->regex, str)) {
	    break;
	}
    }
    if(rp->typ == DMP_SIZE) {
	dump_size = (long)((the_num(str, rp->field)* rp->scale+1023.0)/1024.0);
    }
    switch(rp->typ) {
    case DMP_NORMAL:
	type = "normal";
	startchr = '|';
	break;
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
	/*
	 * Should never get here.
	 */
	type = "unknown";
	startchr = '!';
	break;
    }
    dbprintf(("%s: %3d: %7s(%c): %s\n",
	      debug_prefix_time(NULL),
	      rp->srcline,
	      type,
	      startchr,
	      str));
    fprintf(stderr, "%c %s\n", startchr, str);
}


/*
 * start_index.  Creates an index file from the output of dump/tar.
 * It arranges that input is the fd to be written by the dump process.
 * If createindex is not enabled, it does nothing.  If it is not, a
 * new process will be created that tees input both to a pipe whose
 * read fd is dup2'ed input and to a program that outputs an index
 * file to `index'.
 *
 * make sure that the chat from restore doesn't go to stderr cause
 * this goes back to amanda which doesn't expect to see it
 * (2>/dev/null should do it)
 *
 * Originally by Alan M. McIvor, 13 April 1996
 *
 * Adapted by Alexandre Oliva, 1 May 1997
 *
 * This program owes a lot to tee.c from GNU sh-utils and dumptee.c
 * from the DeeJay backup package.
 */

static void
save_fd(
    int *	fd,
    int		min)
{
  int origfd = *fd;

  while (*fd >= 0 && *fd < min) {
    int newfd = dup(*fd);
    if (newfd == -1)
      dbprintf(("%s: unable to save file descriptor [%s]\n",
	debug_prefix_time(NULL), strerror(errno)));
    *fd = newfd;
  }
  if (origfd != *fd)
    dbprintf(("%s: dupped file descriptor %i to %i\n",
      debug_prefix_time(NULL), origfd, *fd));
}

void
start_index(
    int		createindex,
    int		input,
    int		mesg,
    int		index,
    char *	cmd)
{
  int pipefd[2];
  FILE *pipe_fp;
  int exitcode;

  if (!createindex)
    return;

  if (pipe(pipefd) != 0) {
    error("creating index pipe: %s", strerror(errno));
    /*NOTREACHED*/
  }

  switch(indexpid = fork()) {
  case -1:
    error("forking index tee process: %s", strerror(errno));
    /*NOTREACHED*/

  default:
    aclose(pipefd[0]);
    if (dup2(pipefd[1], input) == -1) {
      error("dup'ping index tee output: %s", strerror(errno));
      /*NOTREACHED*/
    }
    aclose(pipefd[1]);
    return;

  case 0:
    break;
  }

  /* now in a child process */
  save_fd(&pipefd[0], 4);
  save_fd(&index, 4);
  save_fd(&mesg, 4);
  save_fd(&input, 4);
  dup2(pipefd[0], 0);
  dup2(index, 1);
  dup2(mesg, 2);
  dup2(input, 3);
  for(index = 4; index < FD_SETSIZE; index++) {
    if (index != dbfd()) {
      close(index);
    }
  }

  if ((pipe_fp = popen(cmd, "w")) == NULL) {
    error("couldn't start index creator [%s]", strerror(errno));
    /*NOTREACHED*/
  }

  dbprintf(("%s: started index creator: \"%s\"\n",
    debug_prefix_time(NULL), cmd));
  while(1) {
    char buffer[BUFSIZ], *ptr;
    ssize_t bytes_read;
    size_t bytes_written;
    ssize_t just_written;

    do {
	bytes_read = read(0, buffer, SIZEOF(buffer));
    } while ((bytes_read < 0) && ((errno == EINTR) || (errno == EAGAIN)));

    if (bytes_read < 0) {
      error("index tee cannot read [%s]", strerror(errno));
      /*NOTREACHED*/
    }

    if (bytes_read == 0)
      break; /* finished */

    /* write the stuff to the subprocess */
    ptr = buffer;
    bytes_written = 0;
    just_written = fullwrite(fileno(pipe_fp), ptr, (size_t)bytes_read);
    if (just_written < 0) {
	/* 
	 * just as we waited for write() to complete.
	 */
	if (errno != EPIPE) {
	    dbprintf(("%s: index tee cannot write to index creator [%s]\n",
			    debug_prefix_time(NULL), strerror(errno)));
	}
    } else {
	bytes_written += just_written;
	ptr += just_written;
    }

    /* write the stuff to stdout, ensuring none lost when interrupt
       occurs */
    ptr = buffer;
    bytes_written = 0;
    just_written = fullwrite(3, ptr, (size_t)bytes_read);
    if (just_written < 0) {
	error("index tee cannot write [%s]", strerror(errno));
	/*NOTREACHED*/
    } else {
	bytes_written += just_written;
	ptr += just_written;
    }
  }

  aclose(pipefd[1]);

  /* finished */
  /* check the exit code of the pipe and moan if not 0 */
  if ((exitcode = pclose(pipe_fp)) != 0) {
    dbprintf(("%s: index pipe returned %d\n",
      debug_prefix_time(NULL), exitcode));
  } else {
    dbprintf(("%s: index created successfully\n", debug_prefix_time(NULL)));
  }
  pipe_fp = NULL;

  exit(exitcode);
}

extern backup_program_t dump_program, gnutar_program;

backup_program_t *programs[] = {
  &dump_program, &gnutar_program, NULL
};
