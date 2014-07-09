/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
 * Copyright (c) 2007-2013 Zmanda, Inc.  All Rights Reserved.
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
 * $Id: tapefile.h,v 1.9 2006/05/25 01:47:20 johnfranks Exp $
 *
 * interface for active tape list manipulation routines
 */
#ifndef TAPEFILE_H
#define TAPEFILE_H

#include "amanda.h"

typedef struct tape_s {
    struct tape_s *next, *prev;
    int position;
    char * datestamp;
    int reuse;
    char *label;
    char *barcode;
    char *meta;
    guint64 blocksize;
    char *comment;
} tape_t;

int read_tapelist(char *tapefile);
int write_tapelist(char *tapefile);
void clear_tapelist(void);
tape_t *lookup_tapelabel(const char *label);
tape_t *lookup_tapepos(int pos);
tape_t *lookup_tapedate(char *datestamp);
int lookup_nb_tape(void);
char *get_last_reusable_tape_label(int skip);
tape_t *lookup_last_reusable_tape(int skip);
void remove_tapelabel(char *label);
tape_t *add_tapelabel(char *datestamp, char *label, char *comment);
int reusable_tape(tape_t *tp);

int guess_runs_from_tapelist(void);
char *list_new_tapes(int nb);
void print_new_tapes(FILE *output, int nb);

#endif /* !TAPEFILE_H */
