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
 * $Id: amxml.h 5151 2007-02-06 15:41:53Z martineau $
 *
 * xml parsing of amanda protocol packet
 */

#ifndef AMXML_H
#define AMXML_H

#include "conffile.h"

typedef struct script_s {
    char                          *plugin;
    execute_on_t                   execute_on;
    int                            execute_where;
    proplist_t                     property;
    struct client_script_result_s *result;
} script_t;

typedef GSList *scriptlist_t;

typedef struct a_dle_s {
    char   *disk;
    char   *device;
    int     program_is_application_api;
    char   *program;
    int     calcsize;
    estimate_t     estimate;
    int     spindle;
    int     compress;
    int     encrypt;
    int     kencrypt;
    GSList *level;
    int     nb_level;
    char   *dumpdate;
    char   *compprog;
    char   *srv_encrypt;
    char   *clnt_encrypt;
    char   *srv_decrypt_opt;
    char   *clnt_decrypt_opt;
    int     record;
    int     create_index;
    char   *auth;
    sl_t   *exclude_file;
    sl_t   *exclude_list;
    sl_t   *include_file;
    sl_t   *include_list;
    int     exclude_optional;
    int     include_optional;
    proplist_t application_property;
    scriptlist_t scriptlist;
    struct a_dle_s *next;
} dle_t;

dle_t *alloc_dle(void);
void   init_dle(dle_t *dle);
dle_t *amxml_parse_node_CHAR(char *txt, char **errmsg);
dle_t *amxml_parse_node_FILE(FILE *file, char **errmsg);
char  *amxml_format_tag(char *tag, char *value);
#endif
