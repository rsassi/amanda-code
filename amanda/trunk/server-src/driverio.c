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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: driverio.c,v 1.92 2006/08/24 01:57:16 paddy_s Exp $
 *
 * I/O-related functions for driver program
 */
#include "amanda.h"
#include "util.h"
#include "clock.h"
#include "server_util.h"
#include "conffile.h"
#include "diskfile.h"
#include "infofile.h"
#include "logfile.h"

#define GLOBAL		/* the global variables defined here */
#include "driverio.h"

int nb_chunker = 0;

static const char *childstr(int);

void
init_driverio(void)
{
    dumper_t *dumper;

    taper = -1;

    for(dumper = dmptable; dumper < dmptable + MAX_DUMPERS; dumper++) {
	dumper->fd = -1;
    }
}


static const char *
childstr(
    int fd)
{
    static char buf[NUM_STR_SIZE + 32];
    dumper_t *dumper;

    if (fd == taper)
	return ("taper");

    for (dumper = dmptable; dumper < dmptable + MAX_DUMPERS; dumper++) {
	if (dumper->fd == fd)
	    return (dumper->name);
	if (dumper->chunker && dumper->chunker->fd == fd)
	    return (dumper->chunker->name);
    }
    g_snprintf(buf, SIZEOF(buf), _("unknown child (fd %d)"), fd);
    return (buf);
}


void
startup_tape_process(
    char *taper_program)
{
    int    fd[2];
    char **config_options;

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1) {
	error(_("taper pipe: %s"), strerror(errno));
	/*NOTREACHED*/
    }
    if(fd[0] < 0 || fd[0] >= (int)FD_SETSIZE) {
	error(_("taper socketpair 0: descriptor %d out of range (0 .. %d)\n"),
	      fd[0], (int)FD_SETSIZE-1);
        /*NOTREACHED*/
    }
    if(fd[1] < 0 || fd[1] >= (int)FD_SETSIZE) {
	error(_("taper socketpair 1: descriptor %d out of range (0 .. %d)\n"),
	      fd[1], (int)FD_SETSIZE-1);
        /*NOTREACHED*/
    }

    switch(taper_pid = fork()) {
    case -1:
	error(_("fork taper: %s"), strerror(errno));
	/*NOTREACHED*/

    case 0:	/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error(_("taper dup2: %s"), strerror(errno));
	config_options = get_config_options(2);
	config_options[0] = "taper";
	config_options[1] = get_config_name();
	safe_fd(-1, 0);
	execve(taper_program, config_options, safe_env());
	error("exec %s: %s", taper_program, strerror(errno));
	/*NOTREACHED*/

    default:	/* parent process */
	aclose(fd[1]);
	taper = fd[0];
	taper_ev_read = NULL;
    }
}

void
startup_dump_process(
    dumper_t *dumper,
    char *dumper_program)
{
    int    fd[2];
    char **config_options;

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1) {
	error(_("%s pipe: %s"), dumper->name, strerror(errno));
	/*NOTREACHED*/
    }

    switch(dumper->pid = fork()) {
    case -1:
	error(_("fork %s: %s"), dumper->name, strerror(errno));
	/*NOTREACHED*/

    case 0:		/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error(_("%s dup2: %s"), dumper->name, strerror(errno));
	config_options = get_config_options(2);
	config_options[0] = dumper->name ? dumper->name : "dumper",
	config_options[1] = get_config_name();
	safe_fd(-1, 0);
	execve(dumper_program, config_options, safe_env());
	error(_("exec %s (%s): %s"), dumper_program,
	      dumper->name, strerror(errno));
        /*NOTREACHED*/

    default:	/* parent process */
	aclose(fd[1]);
	dumper->fd = fd[0];
	dumper->ev_read = NULL;
	dumper->busy = dumper->down = 0;
	dumper->dp = NULL;
	g_fprintf(stderr,_("driver: started %s pid %u\n"),
		dumper->name, (unsigned)dumper->pid);
	fflush(stderr);
    }
}

void
startup_dump_processes(
    char *dumper_program,
    int inparallel,
    char *timestamp)
{
    int i;
    dumper_t *dumper;
    char number[NUM_STR_SIZE];

    for(dumper = dmptable, i = 0; i < inparallel; dumper++, i++) {
	g_snprintf(number, SIZEOF(number), "%d", i);
	dumper->name = stralloc2("dumper", number);
	dumper->chunker = &chktable[i];
	chktable[i].name = stralloc2("chunker", number);
	chktable[i].dumper = dumper;
	chktable[i].fd = -1;

	startup_dump_process(dumper, dumper_program);
	dumper_cmd(dumper, START, NULL, (void *)timestamp);
    }
}

void
startup_chunk_process(
    chunker_t *chunker,
    char *chunker_program)
{
    int    fd[2];
    char **config_options;

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1) {
	error(_("%s pipe: %s"), chunker->name, strerror(errno));
	/*NOTREACHED*/
    }

    switch(chunker->pid = fork()) {
    case -1:
	error(_("fork %s: %s"), chunker->name, strerror(errno));
	/*NOTREACHED*/

    case 0:		/* child process */
	aclose(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1) {
	    error(_("%s dup2: %s"), chunker->name, strerror(errno));
	    /*NOTREACHED*/
	}
	config_options = get_config_options(2);
	config_options[0] = chunker->name ? chunker->name : "chunker",
	config_options[1] = get_config_name();
	safe_fd(-1, 0);
	execve(chunker_program, config_options, safe_env());
	error(_("exec %s (%s): %s"), chunker_program,
	      chunker->name, strerror(errno));
        /*NOTREACHED*/

    default:	/* parent process */
	aclose(fd[1]);
	chunker->down = 0;
	chunker->fd = fd[0];
	chunker->ev_read = NULL;
	g_fprintf(stderr,_("driver: started %s pid %u\n"),
		chunker->name, (unsigned)chunker->pid);
	fflush(stderr);
    }
}

cmd_t
getresult(
    int fd,
    int show,
    int *result_argc,
    char ***result_argv)
{
    cmd_t t;
    char *line;

    if((line = areads(fd)) == NULL) {
	if(errno) {
	    error(_("reading result from %s: %s"), childstr(fd), strerror(errno));
	    /*NOTREACHED*/
	}
	*result_argc = 0;				/* EOF */
    } else {
	*result_argv = split_quoted_strings(line);
	*result_argc = g_strv_length(*result_argv);
    }

    if(show) {
	g_printf(_("driver: result time %s from %s:"),
	       walltime_str(curclock()),
	       childstr(fd));
	if(line) {
	    g_printf(" %s", line);
	    putchar('\n');
	} else {
	    g_printf(" (eof)\n");
	}
	fflush(stdout);
    }
    amfree(line);

    if(*result_argc < 1) return BOGUS;

    for(t = (cmd_t)(BOGUS+1); t < LAST_TOK; t++)
	if(strcmp((*result_argv)[0], cmdstr[t]) == 0) return t;

    return BOGUS;
}


int
taper_cmd(
    cmd_t cmd,
    void *ptr,
    char *destname,
    int level,
    char *datestamp)
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char splitsize[NUM_STR_SIZE];
    char fallback_splitsize[NUM_STR_SIZE];
    char *diskbuffer = NULL;
    disk_t *dp;
    char *qname;
    char *qdest;

    switch(cmd) {
    case START_TAPER:
	cmdline = vstralloc(cmdstr[cmd], " ", (char *)ptr, "\n", NULL);
	break;
    case FILE_WRITE:
	dp = (disk_t *) ptr;
        qname = quote_string(dp->name);
	qdest = quote_string(destname);
	g_snprintf(number, SIZEOF(number), "%d", level);
	g_snprintf(splitsize, SIZEOF(splitsize), "%lld",
		 (long long)dp->tape_splitsize * 1024);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", qdest,
			    " ", dp->host->hostname,
			    " ", qname,
			    " ", number,
			    " ", datestamp,
			    " ", splitsize,
			    "\n", NULL);
	amfree(qdest);
	amfree(qname);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
        qname = quote_string(dp->name);
	g_snprintf(number, SIZEOF(number), "%d", level);

	/*
          If we haven't been given a place to buffer split dumps to disk,
          make the argument something besides and empty string so's taper
          won't get confused
	*/
	if(!dp->split_diskbuffer || dp->split_diskbuffer[0] == '\0'){
	    diskbuffer = "NULL";
	} else {
	    diskbuffer = dp->split_diskbuffer;
	}
	g_snprintf(splitsize, SIZEOF(splitsize), "%lld",
		 (long long)dp->tape_splitsize * 1024);
	g_snprintf(fallback_splitsize, SIZEOF(fallback_splitsize), "%lld",
		 (long long)dp->fallback_splitsize * 1024);
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", dp->host->hostname,
			    " ", qname,
			    " ", number,
			    " ", datestamp,
			    " ", splitsize,
			    " ", diskbuffer,
			    " ", fallback_splitsize,
			    "\n", NULL);
	amfree(qname);
	break;
    case DONE: /* handle */
    case FAILED: /* handle */
	dp = (disk_t *) ptr;
	cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    "\n", NULL);
	break;
    case NEW_TAPE:
    case NO_NEW_TAPE:
    case QUIT:
	cmdline = stralloc2(cmdstr[cmd], "\n");
	break;
    default:
	error(_("Don't know how to send %s command to taper"), cmdstr[cmd]);
	/*NOTREACHED*/
    }

    /*
     * Note: cmdline already has a '\n'.
     */
    g_printf(_("driver: send-cmd time %s to taper: %s"),
	   walltime_str(curclock()), cmdline);
    fflush(stdout);
    if ((full_write(taper, cmdline, strlen(cmdline))) < strlen(cmdline)) {
	g_printf(_("writing taper command '%s' failed: %s\n"),
		cmdline, strerror(errno));
	fflush(stdout);
	amfree(cmdline);
	return 0;
    }
    if(cmd == QUIT) aclose(taper);
    amfree(cmdline);
    return 1;
}

int
dumper_cmd(
    dumper_t *dumper,
    cmd_t cmd,
    disk_t *dp,
    char   *mesg)
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char numberport[NUM_STR_SIZE];
    char *o;
    char *device;
    char *features;
    char *qname;
    char *qdest;

    switch(cmd) {
    case START:
	cmdline = vstralloc(cmdstr[cmd], " ", mesg, "\n", NULL);
	break;
    case PORT_DUMP:
	if(dp && dp->device) {
	    device = dp->device;
	}
	else {
	    device = "NODEVICE";
	}

	if (dp != NULL) {
	    device = quote_string((dp->device) ? dp->device : "NODEVICE");
	    qname = quote_string(dp->name);
	    g_snprintf(number, SIZEOF(number), "%d", sched(dp)->level);
	    g_snprintf(numberport, SIZEOF(numberport), "%d", dumper->output_port);
	    features = am_feature_to_string(dp->host->features);
	    if (am_has_feature(dp->host->features, fe_req_xml)) {
		o = xml_optionstr(dp, dp->host->features, NULL);
		if (dp->application) {
		    char *app = xml_application(dp->application,
						dp->host->features);
		    vstrextend(&o, app, NULL);
		    amfree(app);
		}
		o = quote_string(o);
	    } else {
		o = optionstr(dp, dp->host->features, NULL);
	    }
	    if ( o == NULL ) {
	      error(_("problem with option string, check the dumptype definition.\n"));
	    }

	    dbprintf("security_driver %s\n", dp->security_driver);
	    cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", numberport,
			    " ", dp->host->hostname,
			    " ", features,
			    " ", qname,
			    " ", device,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", dp->program && strcmp(dp->program,"APPLICATION")!=0 ? dp->program: application_get_plugin(dp->application),
			    " ", dp->amandad_path,
			    " ", dp->client_username,
			    " ", dp->ssh_keys,
			    " ", dp->security_driver,
			    " |", o,
			    "\n", NULL);
	    amfree(features);
	    amfree(o);
	    amfree(qname);
	    amfree(device);
	} else {
		error(_("PORT-DUMP without disk pointer\n"));
		/*NOTREACHED*/
	}
	break;
    case QUIT:
    case ABORT:
	if( dp ) {
	    qdest = quote_string(sched(dp)->destname);
	    cmdline = vstralloc(cmdstr[cmd],
				" ", qdest,
				"\n", NULL );
	    amfree(qdest);
	} else {
	    cmdline = stralloc2(cmdstr[cmd], "\n");
	}
	break;
    default:
	error(_("Don't know how to send %s command to dumper"), cmdstr[cmd]);
	/*NOTREACHED*/
    }

    /*
     * Note: cmdline already has a '\n'.
     */
    if(dumper->down) {
	g_printf(_("driver: send-cmd time %s ignored to down dumper %s: %s"),
	       walltime_str(curclock()), dumper->name, cmdline);
    } else {
	g_printf(_("driver: send-cmd time %s to %s: %s"),
	       walltime_str(curclock()), dumper->name, cmdline);
	fflush(stdout);
	if (full_write(dumper->fd, cmdline, strlen(cmdline)) < strlen(cmdline)) {
	    g_printf(_("writing %s command: %s\n"), dumper->name, strerror(errno));
	    fflush(stdout);
	    amfree(cmdline);
	    return 0;
	}
	if (cmd == QUIT) aclose(dumper->fd);
    }
    amfree(cmdline);
    return 1;
}

int
chunker_cmd(
    chunker_t *chunker,
    cmd_t cmd,
    disk_t *dp,
    char   *mesg)
{
    char *cmdline = NULL;
    char number[NUM_STR_SIZE];
    char chunksize[NUM_STR_SIZE];
    char use[NUM_STR_SIZE];
    char *o;
    int activehd=0;
    assignedhd_t **h=NULL;
    char *features;
    char *qname;
    char *qdest;

    switch(cmd) {
    case START:
	cmdline = vstralloc(cmdstr[cmd], " ", mesg, "\n", NULL);
	break;
    case PORT_WRITE:
	if(dp && sched(dp) && sched(dp)->holdp) {
	    h = sched(dp)->holdp;
	    activehd = sched(dp)->activehd;
	}

	if (dp && h) {
	    qname = quote_string(dp->name);
	    qdest = quote_string(sched(dp)->destname);
	    h[activehd]->disk->allocated_dumpers++;
	    g_snprintf(number, SIZEOF(number), "%d", sched(dp)->level);
	    g_snprintf(chunksize, SIZEOF(chunksize), "%lld",
		    (long long)holdingdisk_get_chunksize(h[0]->disk->hdisk));
	    g_snprintf(use, SIZEOF(use), "%lld",
		    (long long)h[0]->reserved);
	    features = am_feature_to_string(dp->host->features);
	    o = optionstr(dp, dp->host->features, NULL);
	    if ( o == NULL ) {
	      error(_("problem with option string, check the dumptype definition.\n"));
	    }
	    cmdline = vstralloc(cmdstr[cmd],
			    " ", disk2serial(dp),
			    " ", qdest,
			    " ", dp->host->hostname,
			    " ", features,
			    " ", qname,
			    " ", number,
			    " ", sched(dp)->dumpdate,
			    " ", chunksize,
			    " ", dp->program,
			    " ", use,
			    " |", o,
			    "\n", NULL);
	    amfree(features);
	    amfree(o);
	    amfree(qdest);
	    amfree(qname);
	} else {
		error(_("%s command without disk and holding disk.\n"),
		      cmdstr[cmd]);
		/*NOTREACHED*/
	}
	break;
    case CONTINUE:
	if(dp && sched(dp) && sched(dp)->holdp) {
	    h = sched(dp)->holdp;
	    activehd = sched(dp)->activehd;
	}

	if(dp && h) {
	    qname = quote_string(dp->name);
	    qdest = quote_string(h[activehd]->destname);
	    h[activehd]->disk->allocated_dumpers++;
	    g_snprintf(chunksize, SIZEOF(chunksize), "%lld", 
		     (long long)holdingdisk_get_chunksize(h[activehd]->disk->hdisk));
	    g_snprintf(use, SIZEOF(use), "%lld", 
		     (long long)(h[activehd]->reserved - h[activehd]->used));
	    cmdline = vstralloc(cmdstr[cmd],
				" ", disk2serial(dp),
				" ", qdest,
				" ", chunksize,
				" ", use,
				"\n", NULL );
	    amfree(qdest);
	    amfree(qname);
	} else {
	    cmdline = stralloc2(cmdstr[cmd], "\n");
	}
	break;
    case QUIT:
    case ABORT:
	{
	    char *q = quote_string(mesg);
	    cmdline = vstralloc(cmdstr[cmd], " ", q, "\n", NULL);
	    amfree(q);
	}
	break;
    case DONE:
    case FAILED:
	if( dp ) {
	    cmdline = vstralloc(cmdstr[cmd],
				" ", disk2serial(dp),
				"\n",  NULL);
	} else {
	    cmdline = vstralloc(cmdstr[cmd], "\n");
	}
	break;
    default:
	error(_("Don't know how to send %s command to chunker"), cmdstr[cmd]);
	/*NOTREACHED*/
    }

    /*
     * Note: cmdline already has a '\n'.
     */
    g_printf(_("driver: send-cmd time %s to %s: %s"),
	   walltime_str(curclock()), chunker->name, cmdline);
    fflush(stdout);
    if (full_write(chunker->fd, cmdline, strlen(cmdline)) < strlen(cmdline)) {
	g_printf(_("writing %s command: %s\n"), chunker->name, strerror(errno));
	fflush(stdout);
	amfree(cmdline);
	return 0;
    }
    if (cmd == QUIT) aclose(chunker->fd);
    amfree(cmdline);
    return 1;
}

#define MAX_SERIAL MAX_DUMPERS+1	/* one for the taper */

long generation = 1;

struct serial_s {
    long gen;
    disk_t *dp;
} stable[MAX_SERIAL];

disk_t *
serial2disk(
    char *str)
{
    int rc, s;
    long gen;

    rc = sscanf(str, "%d-%ld", &s, &gen);
    if(rc != 2) {
	error(_("error [serial2disk \"%s\" parse error]"), str);
	/*NOTREACHED*/
    } else if (s < 0 || s >= MAX_SERIAL) {
	error(_("error [serial out of range 0..%d: %d]"), MAX_SERIAL, s);
	/*NOTREACHED*/
    }
    if(gen != stable[s].gen)
	g_printf(_("driver: serial2disk error time %s serial gen mismatch %s\n"),
	       walltime_str(curclock()), str);
    return stable[s].dp;
}

void
free_serial(
    char *str)
{
    int rc, s;
    long gen;

    rc = sscanf(str, _("%d-%ld"), &s, &gen);
    if(!(rc == 2 && s >= 0 && s < MAX_SERIAL)) {
	/* nuke self to get core dump for Brett */
	g_fprintf(stderr, _("driver: free_serial: str \"%s\" rc %d s %d\n"),
		str, rc, s);
	fflush(stderr);
	abort();
    }

    if(gen != stable[s].gen)
	g_printf(_("driver: free_serial error time %s serial gen mismatch %s\n"),
	       walltime_str(curclock()),str);
    stable[s].gen = 0;
    stable[s].dp = NULL;
}


void
free_serial_dp(
    disk_t *dp)
{
    int s;

    for(s = 0; s < MAX_SERIAL; s++) {
	if(stable[s].dp == dp) {
	    stable[s].gen = 0;
	    stable[s].dp = NULL;
	    return;
	}
    }

    g_printf(_("driver: error time %s serial not found\n"),
	   walltime_str(curclock()));
}


void
check_unfree_serial(void)
{
    int s;

    /* find used serial number */
    for(s = 0; s < MAX_SERIAL; s++) {
	if(stable[s].gen != 0 || stable[s].dp != NULL) {
	    g_printf(_("driver: error time %s bug: serial in use: %02d-%05ld\n"),
		   walltime_str(curclock()), s, stable[s].gen);
	}
    }
}

char *disk2serial(
    disk_t *dp)
{
    int s;
    static char str[NUM_STR_SIZE];

    for(s = 0; s < MAX_SERIAL; s++) {
	if(stable[s].dp == dp) {
	    g_snprintf(str, SIZEOF(str), "%02d-%05ld", s, stable[s].gen);
	    return str;
	}
    }

    /* find unused serial number */
    for(s = 0; s < MAX_SERIAL; s++)
	if(stable[s].gen == 0 && stable[s].dp == NULL)
	    break;
    if(s >= MAX_SERIAL) {
	g_printf(_("driver: error time %s bug: out of serial numbers\n"),
	       walltime_str(curclock()));
	s = 0;
    }

    stable[s].gen = generation++;
    stable[s].dp = dp;

    g_snprintf(str, SIZEOF(str), "%02d-%05ld", s, stable[s].gen);
    return str;
}

void
update_info_dumper(
     disk_t *dp,
     off_t origsize,
     off_t dumpsize,
     time_t dumptime)
{
    int level, i;
    info_t info;
    stats_t *infp;
    perf_t *perfp;
    char *conf_infofile;

    level = sched(dp)->level;

    conf_infofile = config_dir_relative(getconf_str(CNF_INFOFILE));
    if (open_infofile(conf_infofile)) {
	error(_("could not open info db \"%s\""), conf_infofile);
	/*NOTREACHED*/
    }
    amfree(conf_infofile);

    get_info(dp->host->hostname, dp->name, &info);

    /* Clean up information about this and higher-level dumps.  This
       assumes that update_info_dumper() is always run before
       update_info_taper(). */
    for (i = level; i < DUMP_LEVELS; ++i) {
      infp = &info.inf[i];
      infp->size = (off_t)-1;
      infp->csize = (off_t)-1;
      infp->secs = (time_t)-1;
      infp->date = (time_t)-1;
      infp->label[0] = '\0';
      infp->filenum = 0;
    }

    /* now store information about this dump */
    infp = &info.inf[level];
    infp->size = origsize;
    infp->csize = dumpsize;
    infp->secs = dumptime;
    infp->date = sched(dp)->timestamp;

    if(level == 0) perfp = &info.full;
    else perfp = &info.incr;

    /* Update the stats, but only if the new values are meaningful */
    if(dp->compress != COMP_NONE && origsize > (off_t)0) {
	newperf(perfp->comp, (double)dumpsize/(double)origsize);
    }
    if(dumptime > (time_t)0) {
	if((off_t)dumptime >= dumpsize)
	    newperf(perfp->rate, 1);
	else
	    newperf(perfp->rate, (double)dumpsize/(double)dumptime);
    }

    if(getconf_int(CNF_RESERVE)<100) {
	info.command = NO_COMMAND;
    }

    if(level == info.last_level)
	info.consecutive_runs++;
    else {
	info.last_level = level;
	info.consecutive_runs = 1;
    }

    if(origsize >= (off_t)0 && dumpsize >= (off_t)0) {
	for(i=NB_HISTORY-1;i>0;i--) {
	    info.history[i] = info.history[i-1];
	}

	info.history[0].level = level;
	info.history[0].size  = origsize;
	info.history[0].csize = dumpsize;
	info.history[0].date  = sched(dp)->timestamp;
	info.history[0].secs  = dumptime;
    }

    if(put_info(dp->host->hostname, dp->name, &info)) {
	error(_("infofile update failed (%s,'%s')\n"), dp->host->hostname, dp->name);
	/*NOTREACHED*/
    }

    close_infofile();
}

void
update_info_taper(
    disk_t *dp,
    char *label,
    off_t filenum,
    int level)
{
    info_t info;
    stats_t *infp;
    int rc;

    rc = open_infofile(getconf_str(CNF_INFOFILE));
    if(rc) {
	error(_("could not open infofile %s: %s (%d)"), getconf_str(CNF_INFOFILE),
	      strerror(errno), rc);
	/*NOTREACHED*/
    }

    get_info(dp->host->hostname, dp->name, &info);

    infp = &info.inf[level];
    /* XXX - should we record these two if no-record? */
    strncpy(infp->label, label, SIZEOF(infp->label)-1);
    infp->label[SIZEOF(infp->label)-1] = '\0';
    infp->filenum = filenum;

    info.command = NO_COMMAND;

    if(put_info(dp->host->hostname, dp->name, &info)) {
	error(_("infofile update failed (%s,'%s')\n"), dp->host->hostname, dp->name);
	/*NOTREACHED*/
    }
    close_infofile();
}

/* Free an array of pointers to assignedhd_t after freeing the
 * assignedhd_t themselves. The array must be NULL-terminated.
 */
void free_assignedhd(
    assignedhd_t **ahd)
{
    int i;

    if( !ahd ) { return; }

    for( i = 0; ahd[i]; i++ ) {
	amfree(ahd[i]->destname);
	amfree(ahd[i]);
    }
    amfree(ahd);
}
