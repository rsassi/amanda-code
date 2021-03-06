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
 * $Id: driverio.h,v 1.35 2006/05/25 01:47:19 johnfranks Exp $
 *
 * driver-related helper functions
 */
#ifndef DRIVERIO_H
#define DRIVERIO_H

#include "event.h"

#include "amanda.h"
#include "holding.h"
#include "server_util.h"

#ifndef GLOBAL
#define GLOBAL extern
#endif

/* chunker process structure */

typedef struct chunker_s {
    char *name;			/* name of this chunker */
    pid_t pid;			/* its pid */
    int down;			/* state */
    int fd;			/* read/write */
    int result;
    event_handle_t *ev_read;	/* read event handle */
    struct dumper_s *dumper;
} chunker_t;

/* dumper process structure */

typedef struct dumper_s {
    char *name;			/* name of this dumper */
    pid_t pid;			/* its pid */
    int busy, down;		/* state */
    int fd;			/* read/write */
    int result;
    int output_port;		/* output port */
    event_handle_t *ev_read;	/* read event handle */
    disk_t *dp;			/* disk currently being dumped */
    chunker_t *chunker;
} dumper_t;

typedef struct assignedhd_s {
    holdingdisk_t	*disk;
    off_t		used;
    off_t		reserved;
    char		*destname;
} assignedhd_t;

/* schedule structure */

typedef struct sched_s {
    int attempted, priority;
    int level, degr_level;
    unsigned long est_time, degr_time;
    off_t est_nsize, est_csize, est_size;
    off_t degr_nsize, degr_csize, act_size;
    off_t origsize, dumpsize;
    time_t dumptime, tapetime;
    char *dumpdate, *degr_dumpdate;
    unsigned long est_kps, degr_kps;
    char *destname;				/* file/port name */
    dumper_t *dumper;
    assignedhd_t **holdp;
    time_t timestamp;
    char *datestamp;
    int activehd;
    int no_space;
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)


/* holding disk reservation structure */

typedef struct holdalloc_s {
    int allocated_dumpers;
    off_t allocated_space;
} holdalloc_t;

#define holdalloc(hp)	((holdalloc_t *) (hp)->up)

GLOBAL dumper_t dmptable[MAX_DUMPERS];
GLOBAL chunker_t chktable[MAX_DUMPERS];

/* command/result tokens */

GLOBAL int taper, taper_busy;
GLOBAL pid_t taper_pid;
GLOBAL event_handle_t *taper_ev_read;

void init_driverio(void);
void startup_tape_process(char *taper_program);
void startup_dump_process(dumper_t *dumper, char *dumper_program);
void startup_dump_processes(char *dumper_program, int inparallel, char *timestamp);
void startup_chunk_process(chunker_t *chunker, char *chunker_program);

disk_t *serial2disk(char *str);
void free_serial(char *str);
void free_serial_dp(disk_t *dp);
void check_unfree_serial(void);
char *disk2serial(disk_t *dp);
void update_info_dumper(disk_t *dp, off_t origsize, off_t dumpsize, time_t dumptime);
void update_info_taper(disk_t *dp, char *label, off_t filenum, int level);
void free_assignedhd(assignedhd_t **holdp);
#endif	/* !DRIVERIO_H */
