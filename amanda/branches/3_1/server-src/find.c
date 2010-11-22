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
 * $Id: find.c,v 1.33 2006/07/06 13:13:15 martinea Exp $
 *
 * controlling process for the Amanda backup system
 */
#include "amanda.h"
#include "match.h"
#include "conffile.h"
#include "tapefile.h"
#include "logfile.h"
#include "holding.h"
#include "find.h"
#include <regex.h>
#include "cmdline.h"

int find_match(char *host, char *disk);
static char *find_nicedate(char *datestamp);
static int len_find_nicedate(char *datestamp);
static int find_compare(const void *, const void *);
static int parse_taper_datestamp_log(char *logline, char **datestamp, char **level);
static gboolean logfile_has_tape(char * label, char * datestamp,
                                 char * logfile);

static char *find_sort_order = NULL;

find_result_t * find_dump(disklist_t* diskqp) {
    char *conf_logdir, *logfile = NULL;
    int tape, tape1, maxtape, logs;
    unsigned seq;
    tape_t *tp, *tp1;
    find_result_t *output_find = NULL;
    gboolean *tape_seen = NULL;
    GSList   *label_list;

    conf_logdir = config_dir_relative(getconf_str(CNF_LOGDIR));
    maxtape = lookup_nb_tape();
    tape_seen = g_new0(gboolean, maxtape+1);

    for(tape = 1; tape <= maxtape; tape++) {

	if (tape_seen[tape] == 1)
	    continue;
	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;

	/* find all tape with the same datestamp
	   add them to label_list */
	label_list = NULL;
	for (tape1 = tape; tape1 <= maxtape; tape1++) {
	    tp1 = lookup_tapepos(tape1);
	    if (tp1 == NULL) continue;
	    if (strcmp(tp->datestamp, tp1->datestamp) != 0)
		continue;

	    tape_seen[tape1] = 1;
	    label_list = g_slist_append(label_list, tp1->label);
	}

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    g_snprintf(seq_str, SIZEOF(seq_str), "%u", seq);
	    logfile = newvstralloc(logfile,
			conf_logdir, "/log.", tp->datestamp, ".", seq_str, NULL);
	    if(access(logfile, R_OK) != 0) break;
	    if (search_logfile(&output_find, NULL, tp->datestamp,
                               logfile, diskqp)) {
                logs ++;
            }
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.",
                               tp->datestamp, ".amflush", NULL);
	if(access(logfile,R_OK) == 0) {
	    if (search_logfile(&output_find, NULL, tp->datestamp,
                               logfile, diskqp)) {
                logs ++;
            }
        }
        
	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, conf_logdir, "/log.", tp->datestamp,
                               NULL);
	if(access(logfile,R_OK) == 0) {
	    if (search_logfile(&output_find, NULL, tp->datestamp,
                               logfile, diskqp)) {
                logs ++;
            }
	}
	if (logs == 0 && strcmp(tp->datestamp,"0") != 0) {
	    GSList *l_label;
	    for (l_label = label_list; l_label != NULL ; l_label = l_label->next) {
		g_fprintf(stderr,
                      _("Warning: no log files found for tape %s written %s\n"),
                      (char *)l_label->data, find_nicedate(tp->datestamp));
	    }
	}
	g_slist_free(label_list);
    }
    g_free(tape_seen);
    amfree(logfile);
    amfree(conf_logdir);

    search_holding_disk(&output_find, diskqp);

    return(output_find);
}

char **
find_log(void)
{
    char *conf_logdir, *logfile = NULL;
    char *pathlogfile = NULL;
    int tape, maxtape, logs;
    unsigned seq;
    tape_t *tp;
    char **output_find_log = NULL;
    char **current_log;

    conf_logdir = config_dir_relative(getconf_str(CNF_LOGDIR));
    maxtape = lookup_nb_tape();

    output_find_log = alloc((maxtape*5+10) * SIZEOF(char *));
    current_log = output_find_log;

    for(tape = 1; tape <= maxtape; tape++) {

	tp = lookup_tapepos(tape);
	if(tp == NULL) continue;

	/* search log files */

	logs = 0;

	/* new-style log.<date>.<seq> */

	for(seq = 0; 1; seq++) {
	    char seq_str[NUM_STR_SIZE];

	    g_snprintf(seq_str, SIZEOF(seq_str), "%u", seq);
	    logfile = newvstralloc(logfile, "log.", tp->datestamp, ".", seq_str, NULL);
	    pathlogfile = newvstralloc(pathlogfile, conf_logdir, "/", logfile, NULL);
	    if (access(pathlogfile, R_OK) != 0) break;
	    if (logfile_has_tape(tp->label, tp->datestamp, pathlogfile)) {
		if (current_log == output_find_log || strcmp(*(current_log-1), logfile)) {
		    *current_log = stralloc(logfile);
		    current_log++;
		}
		logs++;
		break;
	    }
	}

	/* search old-style amflush log, if any */

	logfile = newvstralloc(logfile, "log.", tp->datestamp, ".amflush", NULL);
	pathlogfile = newvstralloc(pathlogfile, conf_logdir, "/", logfile, NULL);
	if (access(pathlogfile, R_OK) == 0) {
	    if (logfile_has_tape(tp->label, tp->datestamp, pathlogfile)) {
		if (current_log == output_find_log || strcmp(*(current_log-1), logfile)) {
		    *current_log = stralloc(logfile);
		    current_log++;
		}
		logs++;
	    }
	}

	/* search old-style main log, if any */

	logfile = newvstralloc(logfile, "log.", tp->datestamp, NULL);
	pathlogfile = newvstralloc(pathlogfile, conf_logdir, "/", logfile, NULL);
	if (access(pathlogfile, R_OK) == 0) {
	    if (logfile_has_tape(tp->label, tp->datestamp, pathlogfile)) {
		if (current_log == output_find_log || strcmp(*(current_log-1), logfile)) {
		    *current_log = stralloc(logfile);
		    current_log++;
		}
		logs++;
	    }
	}

	if(logs == 0 && strcmp(tp->datestamp,"0") != 0)
	    g_fprintf(stderr, _("Warning: no log files found for tape %s written %s\n"),
		   tp->label, find_nicedate(tp->datestamp));
    }
    amfree(logfile);
    amfree(pathlogfile);
    amfree(conf_logdir);
    *current_log = NULL;
    return(output_find_log);
}

void
search_holding_disk(
    find_result_t **output_find,
    disklist_t * dynamic_disklist)
{
    GSList *holding_file_list;
    GSList *e;
    char   *holding_file;
    disk_t *dp;
    char   *orig_name;

    holding_file_list = holding_get_files(NULL, 1);

    for(e = holding_file_list; e != NULL; e = e->next) {
	dumpfile_t file;

	holding_file = (char *)e->data;

	if (!holding_file_get_dumpfile(holding_file, &file))
	    continue;

	if (file.dumplevel < 0 || file.dumplevel >= DUMP_LEVELS) {
	    dumpfile_free_data(&file);
	    continue;
	}

	dp = NULL;
	orig_name = g_strdup(file.name);
	for(;;) {
	    char *s;
	    if((dp = lookup_disk(file.name, file.disk)))
		break;
	    if((s = strrchr(file.name,'.')) == NULL)
		break;
	    *s = '\0';
	}
	strcpy(file.name, orig_name); /* restore munged string */
	g_free(orig_name);

	if ( dp == NULL ) {
	    if (dynamic_disklist == NULL) {
		dumpfile_free_data(&file);
		continue;
	    }
	    dp = add_disk(dynamic_disklist, file.name, file.disk);
	    enqueue_disk(dynamic_disklist, dp);
	}

	if(find_match(file.name,file.disk)) {
	    find_result_t *new_output_find = g_new0(find_result_t, 1);
	    new_output_find->next=*output_find;
	    new_output_find->timestamp = stralloc(file.datestamp);
	    new_output_find->hostname = stralloc(file.name);
	    new_output_find->diskname = stralloc(file.disk);
	    new_output_find->level=file.dumplevel;
	    new_output_find->label=stralloc(holding_file);
	    new_output_find->partnum = -1;
	    new_output_find->totalparts = -1;
	    new_output_find->filenum=0;
	    if (file.is_partial) {
		new_output_find->status=stralloc("PARTIAL");
		new_output_find->dump_status=stralloc("PARTIAL");
	    } else {
		new_output_find->status=stralloc("OK");
		new_output_find->dump_status=stralloc("OK");
	    }
	    new_output_find->message=stralloc("");
	    new_output_find->kb = holding_file_size(holding_file, 1);
	    new_output_find->orig_kb = file.orig_size;

	    *output_find=new_output_find;
	}
	dumpfile_free_data(&file);
    }

    g_slist_free_full(holding_file_list);
}

static char *
get_write_timestamp(char *tapelabel)
{
    tape_t *tp;

    if (!tapelabel || !(tp = lookup_tapelabel(tapelabel)))
	return "0";

    return tp->datestamp;
}

static int
find_compare(
    const void *i1,
    const void *j1)
{
    int compare=0;
    find_result_t *i, *j;

    size_t nb_compare=strlen(find_sort_order);
    size_t k;

    for(k=0;k<nb_compare;k++) {
        char sort_key = find_sort_order[k];
        if (isupper((int)sort_key)) {
            /* swap */
            sort_key = tolower(sort_key);
            j = *(find_result_t **)i1;
            i = *(find_result_t **)j1;
        } else {
            i = *(find_result_t **)i1;
            j = *(find_result_t **)j1;
        }            
        
	switch (sort_key) {
	case 'h' : compare=strcmp(i->hostname,j->hostname);
		   break;
	case 'k' : compare=strcmp(i->diskname,j->diskname);
		   break;
	case 'd' : compare=strcmp(i->timestamp,j->timestamp);
		   break;
	case 'l' : compare=j->level - i->level;
		   break;
	case 'f' : compare=(i->filenum == j->filenum) ? 0 :
		           ((i->filenum < j->filenum) ? -1 : 1);
		   break;
	case 'b' : compare=compare_possibly_null_strings(i->label,
                                                         j->label);
                   break;
	case 'w': compare=strcmp(get_write_timestamp(i->label),
				 get_write_timestamp(j->label));
		   break;
	case 'p' :
		   compare=i->partnum - j->partnum;
		   break;
	}
	if(compare != 0)
	    return compare;
    }
    return 0;
}

void
sort_find_result(
    char *sort_order,
    find_result_t **output_find)
{
    find_result_t *output_find_result;
    find_result_t **array_find_result = NULL;
    size_t nb_result=0;
    size_t no_result;

    find_sort_order = sort_order;
    /* qsort core dump if nothing to sort */
    if(*output_find==NULL)
	return;

    /* How many result */
    for(output_find_result=*output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	nb_result++;
    }

    /* put the list in an array */
    array_find_result=alloc(nb_result * SIZEOF(find_result_t *));
    for(output_find_result=*output_find,no_result=0;
	output_find_result;
	output_find_result=output_find_result->next,no_result++) {
	array_find_result[no_result]=output_find_result;
    }

    /* sort the array */
    qsort(array_find_result,nb_result,SIZEOF(find_result_t *),
	  find_compare);

    /* put the sorted result in the list */
    for(no_result=0;
	no_result<nb_result-1; no_result++) {
	array_find_result[no_result]->next = array_find_result[no_result+1];
    }
    array_find_result[nb_result-1]->next=NULL;
    *output_find=array_find_result[0];
    amfree(array_find_result);
}

void
print_find_result(
    find_result_t *output_find)
{
    find_result_t *output_find_result;
    int max_len_datestamp = 4;
    int max_len_hostname  = 4;
    int max_len_diskname  = 4;
    int max_len_level     = 2;
    int max_len_label     =12;
    int max_len_filenum   = 4;
    int max_len_part      = 4;
    int max_len_status    = 6;
    size_t len;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	char *s;

	len=len_find_nicedate(output_find_result->timestamp);
	if((int)len > max_len_datestamp)
	    max_len_datestamp=(int)len;

	len=strlen(output_find_result->hostname);
	if((int)len > max_len_hostname)
	    max_len_hostname = (int)len;

	len = len_quote_string(output_find_result->diskname);
	if((int)len > max_len_diskname)
	    max_len_diskname = (int)len;

        if (output_find_result->label != NULL) {
	    len = len_quote_string(output_find_result->label);
            if((int)len > max_len_label)
                max_len_label = (int)len;
        }

	len=strlen(output_find_result->status) + 1 + strlen(output_find_result->dump_status);
	if((int)len > max_len_status)
	    max_len_status = (int)len;

	s = g_strdup_printf("%d/%d", output_find_result->partnum,
				     output_find_result->totalparts);
	len=strlen(s);
	amfree(s);
	if((int)len > max_len_part)
	    max_len_part = (int)len;
    }

    /*
     * Since status is the rightmost field, we zap the maximum length
     * because it is not needed.  The code is left in place in case
     * another column is added later.
     */
    max_len_status = 1;

    if(output_find==NULL) {
	g_printf(_("\nNo dump to list\n"));
    }
    else {
	g_printf(_("\ndate%*s host%*s disk%*s lv%*s tape or file%*s file%*s part%*s status\n"),
	       max_len_datestamp-4,"",
	       max_len_hostname-4 ,"",
	       max_len_diskname-4 ,"",
	       max_len_level-2    ,"",
	       max_len_label-12   ,"",
	       max_len_filenum-4  ,"",
	       max_len_part-4  ,"");
        for(output_find_result=output_find;
	        output_find_result;
	        output_find_result=output_find_result->next) {
	    char *qdiskname;
            char * formatted_label;
	    char *s;
	    char *status;

	    qdiskname = quote_string(output_find_result->diskname);
            if (output_find_result->label == NULL)
                formatted_label = stralloc("");
	    else
		formatted_label = quote_string(output_find_result->label);

	    if (strcmp(output_find_result->status, "OK") != 0 ||
		strcmp(output_find_result->dump_status, "OK") != 0) {
		status = vstralloc(output_find_result->status, " ",
				   output_find_result->dump_status, NULL);
	    } else {
		status = stralloc(output_find_result->status);
	    }

	    /*@ignore@*/
	    /* sec and kb are omitted here, for compatibility with the existing
	     * output from 'amadmin' */
	    s = g_strdup_printf("%d/%d", output_find_result->partnum,
					 output_find_result->totalparts);
	    g_printf("%-*s %-*s %-*s %*d %-*s %*lld %*s %s %s\n",
                     max_len_datestamp, 
                     find_nicedate(output_find_result->timestamp),
                     max_len_hostname,  output_find_result->hostname,
                     max_len_diskname,  qdiskname,
                     max_len_level,     output_find_result->level,
                     max_len_label,     formatted_label,
                     max_len_filenum,   (long long)output_find_result->filenum,
                     max_len_part,      s,
                                        status,
					output_find_result->message
		    );
	    amfree(status);
	    amfree(s);
	    /*@end@*/
	    amfree(qdiskname);
	    amfree(formatted_label);
	}
    }
}

void
free_find_result(
    find_result_t **output_find)
{
    find_result_t *output_find_result, *prev;

    prev=NULL;
    for(output_find_result=*output_find;
	    output_find_result;
	    output_find_result=output_find_result->next) {
	amfree(prev);
	amfree(output_find_result->timestamp);
	amfree(output_find_result->hostname);
	amfree(output_find_result->diskname);
	amfree(output_find_result->label);
	amfree(output_find_result->status);
	amfree(output_find_result->dump_status);
	amfree(output_find_result->message);
	prev = output_find_result;
    }
    amfree(prev);
    *output_find = NULL;
}

int
find_match(
    char *host,
    char *disk)
{
    disk_t *dp = lookup_disk(host,disk);
    return (dp && dp->todo);
}

static char *
find_nicedate(
    char *datestamp)
{
    static char nice[20];
    int year, month, day;
    int hours, minutes, seconds;
    char date[9], atime[7];
    int  numdate, numtime;

    strncpy(date, datestamp, 8);
    date[8] = '\0';
    numdate = atoi(date);
    year  = numdate / 10000;
    month = (numdate / 100) % 100;
    day   = numdate % 100;

    if(strlen(datestamp) <= 8) {
	g_snprintf(nice, SIZEOF(nice), "%4d-%02d-%02d",
		year, month, day);
    }
    else {
	strncpy(atime, &(datestamp[8]), 6);
	atime[6] = '\0';
	numtime = atoi(atime);
	hours = numtime / 10000;
	minutes = (numtime / 100) % 100;
	seconds = numtime % 100;

	g_snprintf(nice, SIZEOF(nice), "%4d-%02d-%02d %02d:%02d:%02d",
		year, month, day, hours, minutes, seconds);
    }

    return nice;
}

static int
len_find_nicedate(
    char *datestamp)
{
    if(strlen(datestamp) <= 8) {
	return 10;
    } else {
	return 19;
    }
}

static int
parse_taper_datestamp_log(
    char *logline,
    char **datestamp,
    char **label)
{
    char *s;
    int ch;

    s = logline;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    if(strncmp_const_skip(s - 1, "datestamp", s, ch) != 0) {
	return 0;
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    *datestamp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    if(strncmp_const_skip(s - 1, "label", s, ch) != 0) {
	return 0;
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	return 0;
    }
    *label = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    return 1;
}

/* Returns TRUE if the given logfile mentions the given tape. */
static gboolean logfile_has_tape(char * label, char * datestamp,
                                 char * logfile) {
    FILE * logf;
    char * ck_datestamp, *ck_label;
    if((logf = fopen(logfile, "r")) == NULL) {
	error(_("could not open logfile %s: %s"), logfile, strerror(errno));
	/*NOTREACHED*/
    }

    while(get_logline(logf)) {
	if(curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr,
					 &ck_datestamp, &ck_label) == 0) {
		g_printf(_("strange log line \"start taper %s\" curstr='%s'\n"),
                         logfile, curstr);
	    } else if(strcmp(ck_datestamp, datestamp) == 0
		      && strcmp(ck_label, label) == 0) {
                afclose(logf);
                return TRUE;
	    }
	}
    }

    afclose(logf);
    return FALSE;
}

static gboolean
volume_matches(
    const char *label1,
    const char *label2,
    const char *datestamp)
{
    tape_t *tp;

    if (!label2)
	return TRUE;

    if (label1)
	return (strcmp(label1, label2) == 0);

    /* check in tapelist */
    if (!(tp = lookup_tapelabel(label2)))
	return FALSE;

    if (strcmp(tp->datestamp, datestamp) != 0)
	return FALSE;

    return TRUE;
}


/* WARNING: Function accesses globals find_diskqp, curlog, curlog, curstr,
 * dynamic_disklist */
gboolean
search_logfile(
    find_result_t **output_find,
    const char *label,
    const char *passed_datestamp,
    const char *logfile,
    disklist_t * dynamic_disklist)
{
    FILE *logf;
    char *host, *host_undo;
    char *disk, *qdisk, *disk_undo;
    char *date, *date_undo;
    int  partnum;
    int  totalparts;
    int  maxparts = -1;
    char *number;
    int fileno;
    char *current_label = stralloc("");
    char *rest, *rest_undo;
    char *ck_label=NULL;
    int level = 0;
    off_t filenum;
    char *ck_datestamp, *datestamp;
    char *s;
    int ch;
    disk_t *dp;
    find_result_t *part_find = NULL;  /* List for all part of a DLE */
    find_result_t *a_part_find;
    gboolean right_label = FALSE;
    gboolean found_something = FALSE;
    double sec;
    off_t kb;
    off_t orig_kb;

    g_return_val_if_fail(output_find != NULL, 0);
    g_return_val_if_fail(logfile != NULL, 0);

    datestamp = g_strdup(passed_datestamp);

    if((logf = fopen(logfile, "r")) == NULL) {
	error(_("could not open logfile %s: %s"), logfile, strerror(errno));
	/*NOTREACHED*/
    }

    filenum = (off_t)0;
    while(get_logline(logf)) {
	if (curlog == L_START && curprog == P_TAPER) {
	    if(parse_taper_datestamp_log(curstr, &ck_datestamp,
                                         &ck_label) == 0) {
		g_printf(_("strange log line in %s \"start taper %s\"\n"),
                         logfile, curstr);
                continue;
	    }
            if (datestamp != NULL) {
                if (strcmp(datestamp, ck_datestamp) != 0) {
                    g_printf(_("Log file %s stamped %s, expecting %s!\n"),
                             logfile, ck_datestamp, datestamp);
                    break;
                }
            }

            right_label = volume_matches(label, ck_label, ck_datestamp);
	    if (label && datestamp && right_label) {
		found_something = TRUE;
	    }
            amfree(current_label);
            current_label = g_strdup(ck_label);
            if (datestamp == NULL) {
                datestamp = g_strdup(ck_datestamp);
            }
	}
	if (right_label &&
	    (curlog == L_SUCCESS ||
	     curlog == L_CHUNK || curlog == L_PART || curlog == L_PARTPARTIAL) &&
	    curprog == P_TAPER) {
	    filenum++;
	}
	partnum = -1;
	totalparts = -1;
	if (curlog == L_SUCCESS || curlog == L_CHUNKSUCCESS ||
	    curlog == L_DONE    || curlog == L_FAIL ||
	    curlog == L_CHUNK   || curlog == L_PART || curlog == L_PARTIAL ||
	    curlog == L_PARTPARTIAL ) {
	    s = curstr;
	    ch = *s++;

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }

	    if (curlog == L_PART || curlog == L_PARTPARTIAL) {
		char * part_label = s - 1;
		skip_non_whitespace(s, ch);
		s[-1] = '\0';

		if (!right_label)
		    continue;

		if (strcmp(current_label, part_label) != 0) {
		    g_printf("PART label %s doesn't match START label %s\n",
                             part_label, current_label);
		    continue;
		}
		skip_whitespace(s, ch);
		if(ch == '\0') {
		    g_printf("strange log line in %s \"%s\"\n",
			   logfile, curstr);
		    continue;
		}

		number = s - 1;
		skip_non_whitespace(s, ch);
		s[-1] = '\0';
		fileno = atoi(number);
		filenum = fileno;
		if (filenum == 0)
		    continue;

		skip_whitespace(s, ch);
		if(ch == '\0') {
		    g_printf("strange log line in %s \"%s\"\n",
			   logfile, curstr);
		    continue;
		}
	    }

	    host = s - 1;
	    skip_non_whitespace(s, ch);
	    host_undo = s - 1;
	    *host_undo = '\0';

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }
	    qdisk = s - 1;
	    skip_quoted_string(s, ch);
	    disk_undo = s - 1;
	    *disk_undo = '\0';
	    disk = unquote_string(qdisk);

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
                         logfile, curstr);
		continue;
	    }
	    date = s - 1;
	    skip_non_whitespace(s, ch);
	    date_undo = s - 1;
	    *date_undo = '\0';

	    if(strlen(date) < 3) { /* old log didn't have datestamp */
		level = atoi(date);
		date = stralloc(datestamp);
		partnum = 1;
		totalparts =1;
	    } else {
		if (curlog == L_CHUNK || curlog == L_PART ||
		    curlog == L_PARTPARTIAL || curlog == L_DONE){
		    skip_whitespace(s, ch);
		    number = s - 1;
		    skip_non_whitespace(s, ch);
		    s[-1] = '\0';
		    sscanf(number, "%d/%d", &partnum, &totalparts);
		    if (partnum > maxparts)
			maxparts = partnum;
		    if (totalparts > maxparts)
			maxparts = totalparts;
		}
		skip_whitespace(s, ch);
		if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
		    g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		    continue;
		}
		skip_integer(s, ch);
	    }

	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		g_printf(_("strange log line in %s \"%s\"\n"),
		    logfile, curstr);
		continue;
	    }
	    rest = s - 1;
	    skip_non_whitespace(s, ch);
	    rest_undo = s - 1;
	    *rest_undo = '\0';
	    if (strcmp(rest, "[sec") == 0) {
		skip_whitespace(s, ch);
		if(ch == '\0') {
		    g_printf(_("strange log line in %s \"%s\"\n"),
			     logfile, curstr);
		    continue;
		}
		sec = atof(s - 1);
		skip_non_whitespace(s, ch);
		skip_whitespace(s, ch);
		rest = s - 1;
		skip_non_whitespace(s, ch);
		rest_undo = s - 1;
		*rest_undo = '\0';
		if (strcmp(rest, "kb") != 0) {
		    g_printf(_("Bstrange log line in %s \"%s\"\n"),
			     logfile, curstr);
		    continue;
		}

		skip_whitespace(s, ch);
		if (ch == '\0') {
		    g_printf(_("strange log line in %s \"%s\"\n"),
			     logfile, curstr);
		    continue;
		}
		kb = atof(s - 1);
		skip_non_whitespace(s, ch);
		skip_whitespace(s, ch);
		rest = s - 1;
		skip_non_whitespace(s, ch);
		rest_undo = s - 1;
		*rest_undo = '\0';
		if (strcmp(rest, "kps") != 0) {
		    g_printf(_("Cstrange log line in %s \"%s\"\n"),
			     logfile, curstr);
		    continue;
		}

		skip_whitespace(s, ch);
		if (ch == '\0') {
		    g_printf(_("strange log line in %s \"%s\"\n"),
			     logfile, curstr);
		    continue;
		}
		/* kps = atof(s - 1); */
		skip_non_whitespace(s, ch);
		skip_whitespace(s, ch);
		rest = s - 1;
		skip_non_whitespace(s, ch);
		rest_undo = s - 1;
		*rest_undo = '\0';
		if (strcmp(rest, "orig-kb") != 0) {
		    orig_kb = 0;
		} else {
		    skip_whitespace(s, ch);
		    if (ch == '\0') {
			g_printf(_("strange log line in %s \"%s\"\n"),
				 logfile, curstr);
			continue;
		    }
		    orig_kb = atof(s - 1);
                }
	    } else {
		sec = 0;
		kb = 0;
		orig_kb = 0;
		*rest_undo = ' ';
	    }

	    dp = lookup_disk(host,disk);
	    if ( dp == NULL ) {
		if (dynamic_disklist == NULL) {
		    continue;
		}
		dp = add_disk(dynamic_disklist, host, disk);
		enqueue_disk(dynamic_disklist, dp);
	    }
            if (find_match(host, disk)) {
		if(curprog == P_TAPER) {
		    find_result_t *new_output_find = g_new0(find_result_t, 1);
		    new_output_find->timestamp = stralloc(date);
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->partnum = partnum;
		    new_output_find->totalparts = totalparts;
                    new_output_find->label=stralloc(current_label);
		    new_output_find->status=NULL;
		    new_output_find->dump_status=NULL;
		    new_output_find->message=stralloc("");
		    new_output_find->filenum=filenum;
		    new_output_find->sec=sec;
		    new_output_find->kb=kb;
		    new_output_find->orig_kb=orig_kb;
		    new_output_find->next=NULL;
		    if (curlog == L_SUCCESS) {
			new_output_find->status = stralloc("OK");
			new_output_find->dump_status = stralloc("OK");
			new_output_find->next = *output_find;
			*output_find = new_output_find;
                        found_something = TRUE;
		    } else if (curlog == L_CHUNKSUCCESS || curlog == L_DONE ||
			       curlog == L_PARTIAL      || curlog == L_FAIL) {
			/* result line */
			if (curlog == L_PARTIAL || curlog == L_FAIL) {
			    /* set dump_status of each part */
			    for (a_part_find = part_find; a_part_find;
				 a_part_find = a_part_find->next) {
				amfree(a_part_find->dump_status);
				if (curlog == L_PARTIAL)
				    a_part_find->dump_status = stralloc("PARTIAL");
				else {
				    a_part_find->dump_status = stralloc("FAIL");
				    amfree(a_part_find->message);
				    a_part_find->message = stralloc(rest);
				}
			    }
			} else {
			    if (maxparts > -1) { /* format with part */
				/* must check if all part are there */
				int num_part = maxparts;
				for (a_part_find = part_find; a_part_find;
				     a_part_find = a_part_find->next) {
				    if (a_part_find->partnum == num_part &&
					strcmp(a_part_find->status, "OK") == 0)
					num_part--;
			        }
				/* set dump_status of each part */
				for (a_part_find = part_find; a_part_find;
				     a_part_find = a_part_find->next) {
				    amfree(a_part_find->dump_status);
				    if (num_part == 0) {
					a_part_find->dump_status =
						stralloc("OK");
				    } else {
					a_part_find->dump_status =
						stralloc("FAIL");
					amfree(a_part_find->message);
					a_part_find->message =
						stralloc("Missing part");
				    }
				}
			    }
			}
			if (curlog == L_DONE) {
			    for (a_part_find = part_find; a_part_find;
			         a_part_find = a_part_find->next) {
				if (a_part_find->totalparts == -1) {
				    a_part_find->totalparts = maxparts;
				}
				if (a_part_find->orig_kb == 0) {
				    a_part_find->orig_kb = orig_kb;
				}
			    }
			}
			if (part_find) { /* find last element */
			    for (a_part_find = part_find;
				 a_part_find->next != NULL;
				 a_part_find=a_part_find->next) {
			    }
			    /* merge part_find to *output_find */
			    a_part_find->next = *output_find;
			    *output_find = part_find;
			    part_find = NULL;
			    maxparts = -1;
                            found_something = TRUE;
			}
			free_find_result(&new_output_find);
		    } else { /* part line */
			if (curlog == L_PART || curlog == L_CHUNK) {
			    new_output_find->status=stralloc("OK");
			    new_output_find->dump_status=stralloc("OK");
			} else { /* PARTPARTIAL */
			    new_output_find->status=stralloc("PARTIAL");
			    new_output_find->dump_status=stralloc("PARTIAL");
			}
			/* Add to part_find list */
			new_output_find->next = part_find;
			part_find = new_output_find;
			found_something = TRUE;
		    }
		}
		else if(curlog == L_FAIL) {
		    /* print other failures too -- this is a hack to ensure that failures which
		     * did not make it to tape are also listed in the output of 'amadmin x find';
		     * users that do not want this information (e.g., Amanda::DB::Catalog) should
		     * filter dumps with a NULL label. */
		    find_result_t *new_output_find = g_new0(find_result_t, 1);
		    new_output_find->next=*output_find;
		    new_output_find->timestamp = stralloc(date);
		    new_output_find->hostname=stralloc(host);
		    new_output_find->diskname=stralloc(disk);
		    new_output_find->level=level;
		    new_output_find->label=NULL;
		    new_output_find->partnum=partnum;
		    new_output_find->totalparts=totalparts;
		    new_output_find->filenum=0;
		    new_output_find->sec=sec;
		    new_output_find->kb=kb;
		    new_output_find->kb=orig_kb;
		    new_output_find->status=vstralloc(
			 "FAILED (",
			 program_str[(int)curprog],
			 ") ",
			 rest,
			 NULL);
		    new_output_find->dump_status=stralloc("");
		    new_output_find->message=stralloc("");
		    *output_find=new_output_find;
                    found_something = TRUE;
		    maxparts = -1;
		}
	    }
	    amfree(disk);
	}
    }

    /* This could propably be completely removed */
    if (part_find != NULL) {
	if (label) {
	    /* parse log file until PARTIAL/DONE/SUCCESS/FAIL from taper */
	    while(get_logline(logf)) {
		if (curprog == P_TAPER &&
		    (curlog == L_DONE || curlog == L_SUCCESS ||
		     curlog == L_PARTIAL || curlog == L_FAIL)) {
		    break;
		}
	    }
	}
	for (a_part_find = part_find; a_part_find;
	     a_part_find = a_part_find->next) {
	    if (curlog == L_PARTIAL)
		a_part_find->status = stralloc("PARTIAL");
	    else if (curlog == L_FAIL)
		a_part_find->status = stralloc("FAIL");
	    else if (curlog == L_DONE || curlog == L_SUCCESS) {
		if (a_part_find->totalparts == -1) {
		    a_part_find->totalparts = maxparts;
		}
	    }
	}
	for (a_part_find = part_find;
	     a_part_find->next != NULL;
	     a_part_find=a_part_find->next) {
	}
	/* merge part_find to *output_find */
	a_part_find->next = *output_find;
	*output_find = part_find;
	part_find = NULL;
	maxparts = -1;
    }

    afclose(logf);
    amfree(datestamp);
    amfree(current_label);

    return found_something;
}


/*
 * Return the set of dumps that match *all* of the given patterns (we consider
 * an empty pattern to match .*, though).  If 'ok' is true, will only match
 * dumps with SUCCESS status.
 *
 * Returns a newly allocated list of results, where all strings are also newly
 * allocated.  Apparently some part of Amanda leaks under this condition.
 */
find_result_t *
dumps_match(
    find_result_t *output_find,
    char *hostname,
    char *diskname,
    char *datestamp,
    char *level,
    int ok)
{
    find_result_t *cur_result;
    find_result_t *matches = NULL;

    for(cur_result=output_find;
	cur_result;
	cur_result=cur_result->next) {
	char level_str[NUM_STR_SIZE];
	g_snprintf(level_str, SIZEOF(level_str), "%d", cur_result->level);
	if((!hostname || *hostname == '\0' || match_host(hostname, cur_result->hostname)) &&
	   (!diskname || *diskname == '\0' || match_disk(diskname, cur_result->diskname)) &&
	   (!datestamp || *datestamp== '\0' || match_datestamp(datestamp, cur_result->timestamp)) &&
	   (!level || *level== '\0' || match_level(level, level_str)) &&
	   (!ok || !strcmp(cur_result->status, "OK")) &&
	   (!ok || !strcmp(cur_result->dump_status, "OK"))){

	    find_result_t *curmatch = g_new0(find_result_t, 1);
	    memcpy(curmatch, cur_result, SIZEOF(find_result_t));

	    curmatch->timestamp = stralloc(cur_result->timestamp);
	    curmatch->hostname = stralloc(cur_result->hostname);
	    curmatch->diskname = stralloc(cur_result->diskname);
	    curmatch->level = cur_result->level;
	    curmatch->label = cur_result->label? stralloc(cur_result->label) : NULL;
	    curmatch->filenum = cur_result->filenum;
	    curmatch->sec = cur_result->sec;
	    curmatch->kb = cur_result->kb;
	    curmatch->orig_kb = cur_result->orig_kb;
	    curmatch->status = stralloc(cur_result->status);
	    curmatch->dump_status = stralloc(cur_result->dump_status);
	    curmatch->message = stralloc(cur_result->message);
	    curmatch->partnum = cur_result->partnum;
	    curmatch->totalparts = cur_result->totalparts;
	    curmatch->next = matches;
	    matches = curmatch;
	}
    }

    return(matches);
}

/*
 * Return the set of dumps that match one or more of the given dumpspecs,
 * If 'ok' is true, only dumps with a SUCCESS status will be matched.
 * 
 * Returns a newly allocated list of results, where all strings are also newly
 * allocated.  Apparently some part of Amanda leaks under this condition.
 */
find_result_t *
dumps_match_dumpspecs(
    find_result_t *output_find,
    GSList        *dumpspecs,
    int ok)
{
    find_result_t *cur_result;
    find_result_t *matches = NULL;
    GSList        *dumpspec;
    dumpspec_t    *ds;

    for(cur_result=output_find;
	cur_result;
	cur_result=cur_result->next) {
	char level_str[NUM_STR_SIZE];
	char *zeropad_ts = NULL;
	g_snprintf(level_str, SIZEOF(level_str), "%d", cur_result->level);

	/* get the timestamp padded to full width */
	if (strlen(cur_result->timestamp) < 14) {
	    zeropad_ts = g_new0(char, 15);
	    memset(zeropad_ts, '0', 14);
	    memcpy(zeropad_ts, cur_result->timestamp, strlen(cur_result->timestamp));
	}

	for (dumpspec = dumpspecs; dumpspec; dumpspec = dumpspec->next) {
	    ds = (dumpspec_t *)dumpspec->data;
	    if((!ds->host || *ds->host == '\0' || match_host(ds->host, cur_result->hostname)) &&
	       (!ds->disk || *ds->disk == '\0' || match_disk(ds->disk, cur_result->diskname)) &&
	       (!ds->datestamp || *ds->datestamp== '\0'
			|| match_datestamp(ds->datestamp, cur_result->timestamp)
			|| (zeropad_ts && match_datestamp(ds->datestamp, zeropad_ts))) &&
	       (!ds->level || *ds->level== '\0' || match_level(ds->level, level_str)) &&
	       (!ok || !strcmp(cur_result->status, "OK")) &&
	       (!ok || !strcmp(cur_result->dump_status, "OK"))) {

		find_result_t *curmatch = alloc(SIZEOF(find_result_t));
		memcpy(curmatch, cur_result, SIZEOF(find_result_t));

		curmatch->timestamp = stralloc(cur_result->timestamp);
		curmatch->hostname = stralloc(cur_result->hostname);
		curmatch->diskname = stralloc(cur_result->diskname);
		curmatch->level = cur_result->level;
		curmatch->label = cur_result->label? stralloc(cur_result->label) : NULL;
		curmatch->filenum = cur_result->filenum;
		curmatch->status = stralloc(cur_result->status);
		curmatch->dump_status = stralloc(cur_result->dump_status);
		curmatch->message = stralloc(cur_result->message);
		curmatch->partnum = cur_result->partnum;
		curmatch->totalparts = cur_result->totalparts;

		curmatch->next = matches;
		matches = curmatch;
		break;
	    }
	}

	amfree(zeropad_ts);
    }

    return(matches);
}

find_result_t *
dump_exist(
    find_result_t *output_find,
    char *hostname,
    char *diskname,
    char *datestamp,
    int level)
{
    find_result_t *output_find_result;

    for(output_find_result=output_find;
	output_find_result;
	output_find_result=output_find_result->next) {
	if( !strcmp(output_find_result->hostname, hostname) &&
	    !strcmp(output_find_result->diskname, diskname) &&
	    !strcmp(output_find_result->timestamp, datestamp) &&
	    output_find_result->level == level) {

	    return output_find_result;
	}
    }
    return(NULL);
}
