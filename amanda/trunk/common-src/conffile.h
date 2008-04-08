/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
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
 *                         Computer Science Department
 *                         University of Maryland at College Park
 */
/*
 * $Id: conffile.h,v 1.72 2006/07/26 15:17:37 martinea Exp $
 *
 * interface for config file reading code
 */
#ifndef CONFFILE_H
#define CONFFILE_H

#include "amanda.h"
#include "util.h"

/* Getting Configuration Values
 * ============================
 *
 * Amanda configurations consist of a number of "global" parameters, as well as named
 * subsections of four types: dumptypes, interfaces, holdingdisks, and tapetypes.  The
 * global parameters are fetched with the getconf_CONFTYPE functions, keyed by a
 * confparam_t constant (with prefix CNF_).  The subsection parameters are fetched with
 * SUBSEC_get_PARAM() macros, e.g., tapetype_get_blocksize(ttyp), where the argument
 * comes from lookup_SUBSEC(), in this case lookup_tapetype(name).
 *
 * Types
 * =====
 *
 * This module juggles two kinds of types: C types and conftypes.  Conftypes include
 * everything from integers through property lists, and are specific to the needs of
 * the configuration system.  Each conftype has a corresponding C type, which is of course
 * necessary to actually use the data.
 *
 * The val_t__CONFTYPE macros represent the canonical correspondance of conftypes to C
 * types, but in general the relationship is obvious: ints, strings, reals, and so forth
 * are represented directly.  Enumerated conftypes are represented by the corresponding
 * C enum type.  The 'rate' conftype is represented as a 2-element array of doubles, and
 * the 'intrange' conftype is represented as a 2-element array of ints.  exincludes are
 * a exinclude_t *, and a proplist is represented as a GHashTable *.
 *
 * Memory
 * ======
 * Note that, unless specified, all memory in this module is managed by the module
 * itself; return strings should not be freed by the caller.
 */

/*
 * Generic values
 *
 * This module uses a generic val_t type to hold values of various types -- it's basically
 * a union with type information and a 'seen' flag.  In a way, it's a very simple equivalent
 * to Glib's GValue.  It's worth considering rewriting this with GValue, but for the moment,
 * this works and it's here.
 */

/* holdingdisk types */
typedef enum {
    HOLD_NEVER,                 /* Always direct to tape  */
    HOLD_AUTO,                  /* If possible            */
    HOLD_REQUIRED               /* Always to holding disk */
} dump_holdingdisk_t;

/* Compression types */
typedef enum {
    COMP_NONE,          /* No compression */
    COMP_FAST,          /* Fast compression on client */
    COMP_BEST,          /* Best compression on client */
    COMP_CUST,          /* Custom compression on client */
    COMP_SERVER_FAST,   /* Fast compression on server */
    COMP_SERVER_BEST,   /* Best compression on server */
    COMP_SERVER_CUST    /* Custom compression on server */
} comp_t;

/* Encryption types */
typedef enum {
    ENCRYPT_NONE,               /* No encryption */
    ENCRYPT_CUST,               /* Custom encryption on client */
    ENCRYPT_SERV_CUST           /* Custom encryption on server */
} encrypt_t;

/* Estimate strategies */
typedef enum {
    ES_CLIENT,          /* client estimate */
    ES_SERVER,          /* server estimate */
    ES_CALCSIZE,        /* calcsize estimate */
    ES_ES /* sentinel */
} estimate_t;

/* Dump strategies */
typedef enum {
    DS_SKIP,        /* Don't do any dumps at all */
    DS_STANDARD,    /* Standard (0 1 1 1 1 2 2 2 ...) */
    DS_NOFULL,      /* No full's (1 1 1 ...) */
    DS_NOINC,       /* No inc's (0 0 0 ...) */
    DS_4,           /* ? (0 1 2 3 4 5 6 7 8 9 10 11 ...) */
    DS_5,           /* ? (0 1 1 1 1 1 1 1 1 1 1 1 ...) */
    DS_HANOI,       /* Tower of Hanoi (? ? ? ? ? ...) */
    DS_INCRONLY,    /* Forced fulls (0 1 1 2 2 FORCE0 1 1 ...) */
    DS_DS /* sentinel */
} strategy_t;

typedef enum {
    ALGO_FIRST,
    ALGO_FIRSTFIT,
    ALGO_LARGEST,
    ALGO_LARGESTFIT,
    ALGO_SMALLEST,
    ALGO_LAST,
    ALGO_ALGO /* sentinel */
} taperalgo_t;

/* execute_on types */
#define EXECUTE_ON_PRE_DLE_AMCHECK     1<<0
#define EXECUTE_ON_PRE_HOST_AMCHECK    1<<1
#define EXECUTE_ON_POST_DLE_AMCHECK    1<<2
#define EXECUTE_ON_POST_HOST_AMCHECK   1<<3
#define EXECUTE_ON_PRE_DLE_ESTIMATE    1<<4
#define EXECUTE_ON_PRE_HOST_ESTIMATE   1<<5
#define EXECUTE_ON_POST_DLE_ESTIMATE   1<<6
#define EXECUTE_ON_POST_HOST_ESTIMATE  1<<7
#define EXECUTE_ON_PRE_DLE_BACKUP      1<<8
#define EXECUTE_ON_PRE_HOST_BACKUP     1<<9
#define EXECUTE_ON_POST_DLE_BACKUP     1<<10
#define EXECUTE_ON_POST_HOST_BACKUP    1<<11
typedef int execute_on_t;

typedef int execute_where_t;

typedef struct exinclude_s {
    sl_t *sl_list;
    sl_t *sl_file;
    int  optional;
} exinclude_t;

typedef GHashTable* proplist_t;
typedef GSList* pp_scriptlist_t;

/* Names for the type of value in a val_t.  Mostly for internal use, but useful
 * for wrapping val_t's, too. */
typedef enum {
    CONFTYPE_INT,
    CONFTYPE_AM64,
    CONFTYPE_REAL,
    CONFTYPE_STR,
    CONFTYPE_IDENT,
    CONFTYPE_TIME,
    CONFTYPE_SIZE,
    CONFTYPE_BOOLEAN,
    CONFTYPE_COMPRESS,
    CONFTYPE_ENCRYPT,
    CONFTYPE_HOLDING,
    CONFTYPE_ESTIMATE,
    CONFTYPE_STRATEGY,
    CONFTYPE_TAPERALGO,
    CONFTYPE_PRIORITY,
    CONFTYPE_RATE,
    CONFTYPE_INTRANGE,
    CONFTYPE_EXINCLUDE,
    CONFTYPE_PROPLIST,
    CONFTYPE_APPLICATION,
    CONFTYPE_EXECUTE_ON,
    CONFTYPE_EXECUTE_WHERE,
    CONFTYPE_PP_SCRIPTLIST
} conftype_t;

/* This should be considered an opaque type for any other modules.  The complete
 * struct is included here to allow quick access via macros. Access it *only* through
 * those macros. */
typedef struct val_s {
    union {
        int		i;
        off_t		am64;
        double		r;
        char		*s;
        ssize_t		size;
        time_t		t;
        float		rate[2];
        exinclude_t	exinclude;
        int		intrange[2];
        proplist_t      proplist;
	struct application_s  *application;
	pp_scriptlist_t pp_scriptlist;
    } v;
    int seen;
    conftype_t type;
} val_t;

/* Functions to typecheck and extract a particular type of
 * value from a val_t.  All call error() if the type is incorrect,
 * as this is a programming error.  */
int                   val_t_to_int      (val_t *);
off_t                 val_t_to_am64     (val_t *);
float                 val_t_to_real     (val_t *);
char                 *val_t_to_str      (val_t *); /* (also converts CONFTYPE_IDENT) */
char                 *val_t_to_ident    (val_t *); /* (also converts CONFTYPE_STR) */
time_t                val_t_to_time     (val_t *);
ssize_t               val_t_to_size     (val_t *);
int                   val_t_to_boolean  (val_t *);
comp_t                val_t_to_compress (val_t *);
encrypt_t             val_t_to_encrypt  (val_t *);
dump_holdingdisk_t    val_t_to_holding  (val_t *);
estimate_t            val_t_to_estimate (val_t *);
strategy_t            val_t_to_strategy (val_t *);
taperalgo_t           val_t_to_taperalgo(val_t *);
int                   val_t_to_priority (val_t *);
float                *val_t_to_rate     (val_t *); /* array of two floats */
exinclude_t           val_t_to_exinclude(val_t *);
int                  *val_t_to_intrange (val_t *); /* array of two ints */
proplist_t            val_t_to_proplist (val_t *);
struct application_s *val_t_to_application(val_t *);
pp_scriptlist_t       val_t_to_pp_scriptlist(val_t *);
execute_on_t          val_t_to_execute_on(val_t *);
execute_where_t       val_t_to_execute_where(val_t *);

/* Has the given val_t been seen in a configuration file or config overwrite?
 *
 * @param val: val_t* to examine
 * @returns: boolean
 */
#define val_t_seen(val) ((val)->seen)

/* What is the underlying type of this val_t?
 *
 * @param val: val_t* to examine
 * @returns: conftype_t
 */
#define val_t_type(val) ((val)->type)

/* Macros to convert val_t's to a particular type without the benefit of
 * a typecheck.  Use these only if you really know what you're doing!
 *
 * Implementation note: these macros encode the relationship of conftypes
 * (in the macro name) to the corresponding union field.  The macros work
 * as lvalues, too.
 */
#define val_t__seen(val)          ((val)->seen)
#define val_t__int(val)           ((val)->v.i)
#define val_t__am64(val)          ((val)->v.am64)
#define val_t__real(val)          ((val)->v.r)
#define val_t__str(val)           ((val)->v.s)
#define val_t__ident(val)         ((val)->v.s)
#define val_t__time(val)          ((val)->v.t)
#define val_t__size(val)          ((val)->v.size)
#define val_t__boolean(val)       ((val)->v.i)
#define val_t__compress(val)      ((val)->v.i)
#define val_t__encrypt(val)       ((val)->v.i)
#define val_t__holding(val)       ((val)->v.i)
#define val_t__estimate(val)      ((val)->v.i)
#define val_t__strategy(val)      ((val)->v.i)
#define val_t__taperalgo(val)     ((val)->v.i)
#define val_t__priority(val)      ((val)->v.i)
#define val_t__rate(val)          ((val)->v.rate)
#define val_t__exinclude(val)     ((val)->v.exinclude)
#define val_t__intrange(val)      ((val)->v.intrange)
#define val_t__proplist(val)      ((val)->v.proplist)
#define val_t__pp_scriptlist(val) ((val)->v.pp_scriptlist)
#define val_t__application(val)   ((val)->v.application)
#define val_t__execute_on(val)    ((val)->v.i)
#define val_t__execute_where(val) ((val)->v.i)
/*
 * Parameters
 *
 * Programs get val_t's by giving the index of the parameters they're interested in.
 * For global parameters, these start with CNF; for subsections, they start with the
 * name of the subsection.
 */

/*
 * Global parameter access
 */
typedef enum {
    CNF_ORG,
    CNF_CONF,
    CNF_INDEX_SERVER,
    CNF_TAPE_SERVER,
    CNF_AUTH,
    CNF_SSH_KEYS,
    CNF_AMANDAD_PATH,
    CNF_CLIENT_USERNAME,
    CNF_GNUTAR_LIST_DIR,
    CNF_AMANDATES,
    CNF_MAILTO,
    CNF_DUMPUSER,
    CNF_TAPEDEV,
    CNF_RAWTAPEDEV,
    CNF_DEVICE_PROPERTY,
    CNF_PROPERTY,
    CNF_APPLICATION,
    CNF_APPLICATION_TOOL,
    CNF_EXECUTE_ON,
    CNF_PP_SCRIPT,
    CNF_PP_SCRIPT_TOOL,
    CNF_PLUGIN,
    CNF_CHANGERDEV,
    CNF_CHANGERFILE,
    CNF_LABELSTR,
    CNF_TAPELIST,
    CNF_DISKFILE,
    CNF_INFOFILE,
    CNF_LOGDIR,
    CNF_INDEXDIR,
    CNF_TAPETYPE,
    CNF_DUMPCYCLE,
    CNF_RUNSPERCYCLE,
    CNF_TAPECYCLE,
    CNF_NETUSAGE,
    CNF_INPARALLEL,
    CNF_DUMPORDER,
    CNF_BUMPPERCENT,
    CNF_BUMPSIZE,
    CNF_BUMPMULT,
    CNF_BUMPDAYS,
    CNF_TPCHANGER,
    CNF_RUNTAPES,
    CNF_MAXDUMPS,
    CNF_ETIMEOUT,
    CNF_DTIMEOUT,
    CNF_CTIMEOUT,
    CNF_TAPEBUFS,
    CNF_DEVICE_OUTPUT_BUFFER_SIZE,
    CNF_PRINTER,
    CNF_AUTOFLUSH,
    CNF_RESERVE,
    CNF_MAXDUMPSIZE,
    CNF_COLUMNSPEC,
    CNF_AMRECOVER_DO_FSF,
    CNF_AMRECOVER_CHECK_LABEL,
    CNF_AMRECOVER_CHANGER,
    CNF_TAPERALGO,
    CNF_FLUSH_THRESHOLD_DUMPED,
    CNF_FLUSH_THRESHOLD_SCHEDULED,
    CNF_TAPERFLUSH,
    CNF_DISPLAYUNIT,
    CNF_KRB5KEYTAB,
    CNF_KRB5PRINCIPAL,
    CNF_LABEL_NEW_TAPES,
    CNF_USETIMESTAMPS,
    CNF_REP_TRIES,
    CNF_CONNECT_TRIES,
    CNF_REQ_TRIES,
    CNF_DEBUG_AMANDAD,
    CNF_DEBUG_AMIDXTAPED,
    CNF_DEBUG_AMINDEXD,
    CNF_DEBUG_AMRECOVER,
    CNF_DEBUG_AUTH,
    CNF_DEBUG_EVENT,
    CNF_DEBUG_HOLDING,
    CNF_DEBUG_PROTOCOL,
    CNF_DEBUG_PLANNER,
    CNF_DEBUG_DRIVER,
    CNF_DEBUG_DUMPER,
    CNF_DEBUG_CHUNKER,
    CNF_DEBUG_TAPER,
    CNF_DEBUG_SELFCHECK,
    CNF_DEBUG_SENDSIZE,
    CNF_DEBUG_SENDBACKUP,
    CNF_RESERVED_UDP_PORT,
    CNF_RESERVED_TCP_PORT,
    CNF_UNRESERVED_TCP_PORT,
    CNF_CNF /* sentinel */
} confparm_key;

/* Given a confparm_key, return a pointer to the corresponding val_t.
 *
 * @param key: confparm_key
 * @returns: pointer to value
 */
val_t *getconf(confparm_key key);

/* (convenience macro) has this global parameter been seen?
 *
 * @param key: confparm_key
 * @returns: boolean
 */
#define getconf_seen(key)       (val_t_seen(getconf((key))))

/* (convenience macros)
 * Fetch a gloabl parameter of a specific type.  Note that these
 * convenience macros have a different form from those for the
 * subsections: here you specify a type and a key, while for the
 * subsections you specify only a key.  The difference is historical.
 *
 * @param key: confparm_key
 * @returns: various
 */
#define getconf_int(key)          (val_t_to_int(getconf((key))))
#define getconf_am64(key)         (val_t_to_am64(getconf((key))))
#define getconf_real(key)         (val_t_to_real(getconf((key))))
#define getconf_str(key)	  (val_t_to_str(getconf((key))))
#define getconf_ident(key)        (val_t_to_ident(getconf((key))))
#define getconf_time(key)         (val_t_to_time(getconf((key))))
#define getconf_size(key)         (val_t_to_size(getconf((key))))
#define getconf_boolean(key)      (val_t_to_boolean(getconf((key))))
#define getconf_compress(key)     (val_t_to_compress(getconf((key))))
#define getconf_encrypt(key)      (val_t_to_encrypt(getconf((key))))
#define getconf_holding(key)      (val_t_to_holding(getconf((key))))
#define getconf_estimate(key)     (val_t_to_estimate(getconf((key))))
#define getconf_strategy(key)     (val_t_to_strategy(getconf((key))))
#define getconf_taperalgo(key)    (val_t_to_taperalgo(getconf((key))))
#define getconf_priority(key)     (val_t_to_priority(getconf((key))))
#define getconf_rate(key)         (val_t_to_rate(getconf((key))))
#define getconf_exinclude(key)    (val_t_to_exinclude(getconf((key))))
#define getconf_intrange(key)     (val_t_to_intrange(getconf((key))))
#define getconf_proplist(key)     (val_t_to_proplist(getconf((key))))

/* Get a list of names for subsections of the given type
 *
 * @param listname: the desired type of subsection
 * @returns: list of subsection names; caller is responsible for freeing
 * this list, but not the strings it points to, using g_slist_free().
 */
GSList *getconf_list(char *listname);

/* Get a configuration value by name, supporting the TYPE:SUBSEC:KEYWORD.
 * Returns NULL if the configuration value doesnt exist.
 */
val_t *getconf_byname(char *key);

/*
 * Derived values
 *
 * Values which aren't directly specified by the configuration, but which
 * are derived from it.
 */

/* Return a divisor which will convert a value in units of kilo-whatevers
 * to the user's selected display unit.
 *
 * @returns: long integer divisor
 */
long int getconf_unit_divisor(void);

/* If any of these globals are true, the corresponding component will
 * send verbose debugging output to the debug file.  The options are
 * set during config_init, but can be modified at will after that if 
 * desired.  */

extern int debug_amandad;
extern int debug_amidxtaped;
extern int debug_amindexd;
extern int debug_amrecover;
extern int debug_auth;
extern int debug_event;
extern int debug_holding;
extern int debug_protocol;
extern int debug_planner;
extern int debug_driver;
extern int debug_dumper;
extern int debug_chunker;
extern int debug_taper;
extern int debug_selfcheck;
extern int debug_sendsize;
extern int debug_sendbackup;

/*
 * Tapetype parameter access
 */

typedef enum {
    TAPETYPE_COMMENT,
    TAPETYPE_LBL_TEMPL,
    TAPETYPE_BLOCKSIZE,
    TAPETYPE_READBLOCKSIZE,
    TAPETYPE_LENGTH,
    TAPETYPE_FILEMARK,
    TAPETYPE_SPEED,
    TAPETYPE_FILE_PAD,
    TAPETYPE_TAPETYPE /* sentinel */
} tapetype_key;

/* opaque object */
typedef struct tapetype_s tapetype_t;

/* Given the name of the tapetype, return a tapetype object.  Returns NULL
 * if no matching tapetype exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired tapetype
 * @returns: object or NULL
 */
tapetype_t *lookup_tapetype(char *identifier);

/* Given a tapetype and a key, return a pointer to the corresponding val_t.
 *
 * @param ttyp: the tapetype to examine
 * @param key: tapetype_key (one of the TAPETYPE_* constants)
 * @returns: pointer to value
 */
val_t *tapetype_getconf(tapetype_t *ttyp, tapetype_key key);

/* Get the name of this tapetype.
 *
 * @param ttyp: the tapetype to examine
 * @returns: name of the tapetype
 */
char *tapetype_name(tapetype_t *ttyp);

/* (convenience macro) has this parameter been seen in this tapetype?  This
 * applies to the specific parameter *within* the tapetype.
 *
 * @param key: tapetype_key
 * @returns: boolean
 */
#define tapetype_seen(ttyp, key)       (val_t_seen(tapetype_getconf((ttyp), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param ttyp: the tapetype to examine
 * @returns: various
 */
#define tapetype_get_comment(ttyp)         (val_t_to_str(tapetype_getconf((ttyp), TAPETYPE_COMMENT)))
#define tapetype_get_lbl_templ(ttyp)       (val_t_to_str(tapetype_getconf((ttyp), TAPETYPE_LBL_TEMPL)))
#define tapetype_get_blocksize(ttyp)       (val_t_to_size(tapetype_getconf((ttyp), TAPETYPE_BLOCKSIZE)))
#define tapetype_get_readblocksize(ttyp)   (val_t_to_size(tapetype_getconf((ttyp), TAPETYPE_READBLOCKSIZE)))
#define tapetype_get_length(ttyp)          (val_t_to_am64(tapetype_getconf((ttyp), TAPETYPE_LENGTH)))
#define tapetype_get_filemark(ttyp)        (val_t_to_am64(tapetype_getconf((ttyp), TAPETYPE_FILEMARK)))
#define tapetype_get_speed(ttyp)           (val_t_to_int(tapetype_getconf((ttyp), TAPETYPE_SPEED)))
#define tapetype_get_file_pad(ttyp)        (val_t_to_boolean(tapetype_getconf((ttyp), TAPETYPE_FILE_PAD)))

/*
 * Dumptype parameter access
 */

typedef enum {
    DUMPTYPE_COMMENT,
    DUMPTYPE_PROGRAM,
    DUMPTYPE_SRVCOMPPROG,
    DUMPTYPE_CLNTCOMPPROG,
    DUMPTYPE_SRV_ENCRYPT,
    DUMPTYPE_CLNT_ENCRYPT,
    DUMPTYPE_AMANDAD_PATH,
    DUMPTYPE_CLIENT_USERNAME,
    DUMPTYPE_SSH_KEYS,
    DUMPTYPE_SECURITY_DRIVER,
    DUMPTYPE_EXCLUDE,
    DUMPTYPE_INCLUDE,
    DUMPTYPE_PRIORITY,
    DUMPTYPE_DUMPCYCLE,
    DUMPTYPE_MAXDUMPS,
    DUMPTYPE_MAXPROMOTEDAY,
    DUMPTYPE_BUMPPERCENT,
    DUMPTYPE_BUMPSIZE,
    DUMPTYPE_BUMPDAYS,
    DUMPTYPE_BUMPMULT,
    DUMPTYPE_STARTTIME,
    DUMPTYPE_STRATEGY,
    DUMPTYPE_ESTIMATE,
    DUMPTYPE_COMPRESS,
    DUMPTYPE_ENCRYPT,
    DUMPTYPE_SRV_DECRYPT_OPT,
    DUMPTYPE_CLNT_DECRYPT_OPT,
    DUMPTYPE_COMPRATE,
    DUMPTYPE_TAPE_SPLITSIZE,
    DUMPTYPE_FALLBACK_SPLITSIZE,
    DUMPTYPE_SPLIT_DISKBUFFER,
    DUMPTYPE_RECORD,
    DUMPTYPE_SKIP_INCR,
    DUMPTYPE_SKIP_FULL,
    DUMPTYPE_HOLDINGDISK,
    DUMPTYPE_KENCRYPT,
    DUMPTYPE_IGNORE,
    DUMPTYPE_INDEX,
    DUMPTYPE_APPLICATION,
    DUMPTYPE_PP_SCRIPTLIST,
    DUMPTYPE_PROPERTY,
    DUMPTYPE_DUMPTYPE /* sentinel */
} dumptype_key;

/* opaque object */
typedef struct dumptype_s dumptype_t;

/* Given the name of the dumptype, return a dumptype object.  Returns NULL
 * if no matching dumptype exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired dumptype
 * @returns: object or NULL
 */
dumptype_t *lookup_dumptype(char *identifier);

/* Given a dumptype and a key, return a pointer to the corresponding val_t.
 *
 * @param dtyp: the dumptype to examine
 * @param key: dumptype_key (one of the TAPETYPE_* constants)
 * @returns: pointer to value
 */
val_t *dumptype_getconf(dumptype_t *dtyp, dumptype_key key);

/* Get the name of this dumptype.
 *
 * @param dtyp: the dumptype to examine
 * @returns: name of the dumptype
 */
char *dumptype_name(dumptype_t *dtyp);

/* (convenience macro) has this parameter been seen in this dumptype?  This
 * applies to the specific parameter *within* the dumptype.
 *
 * @param key: dumptype_key
 * @returns: boolean
 */
#define dumptype_seen(dtyp, key)       (val_t_seen(dumptype_getconf((dtyp), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param dtyp: the dumptype to examine
 * @returns: various
 */
#define dumptype_get_comment(dtyp)             (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_COMMENT)))
#define dumptype_get_program(dtyp)             (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_PROGRAM)))
#define dumptype_get_srvcompprog(dtyp)         (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SRVCOMPPROG)))
#define dumptype_get_clntcompprog(dtyp)        (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_CLNTCOMPPROG)))
#define dumptype_get_srv_encrypt(dtyp)         (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SRV_ENCRYPT)))
#define dumptype_get_clnt_encrypt(dtyp)        (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_CLNT_ENCRYPT)))
#define dumptype_get_amandad_path(dtyp)        (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_AMANDAD_PATH)))
#define dumptype_get_client_username(dtyp)     (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_CLIENT_USERNAME)))
#define dumptype_get_ssh_keys(dtyp)            (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SSH_KEYS)))
#define dumptype_get_security_driver(dtyp)     (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SECURITY_DRIVER)))
#define dumptype_get_exclude(dtyp)             (val_t_to_exinclude(dumptype_getconf((dtyp), DUMPTYPE_EXCLUDE)))
#define dumptype_get_include(dtyp)             (val_t_to_exinclude(dumptype_getconf((dtyp), DUMPTYPE_INCLUDE)))
#define dumptype_get_priority(dtyp)            (val_t_to_priority(dumptype_getconf((dtyp), DUMPTYPE_PRIORITY)))
#define dumptype_get_dumpcycle(dtyp)           (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_DUMPCYCLE)))
#define dumptype_get_maxcycle(dtyp)            (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_MAXCYCLE)))
#define dumptype_get_frequency(dtyp)           (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_FREQUENCY)))
#define dumptype_get_maxdumps(dtyp)            (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_MAXDUMPS)))
#define dumptype_get_maxpromoteday(dtyp)       (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_MAXPROMOTEDAY)))
#define dumptype_get_bumppercent(dtyp)         (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_BUMPPERCENT)))
#define dumptype_get_bumpsize(dtyp)            (val_t_to_am64(dumptype_getconf((dtyp), DUMPTYPE_BUMPSIZE)))
#define dumptype_get_bumpdays(dtyp)            (val_t_to_int(dumptype_getconf((dtyp), DUMPTYPE_BUMPDAYS)))
#define dumptype_get_bumpmult(dtyp)            (val_t_to_real(dumptype_getconf((dtyp), DUMPTYPE_BUMPMULT)))
#define dumptype_get_starttime(dtyp)           (val_t_to_time(dumptype_getconf((dtyp), DUMPTYPE_STARTTIME)))
#define dumptype_get_strategy(dtyp)            (val_t_to_strategy(dumptype_getconf((dtyp), DUMPTYPE_STRATEGY)))
#define dumptype_get_estimate(dtyp)            (val_t_to_estimate(dumptype_getconf((dtyp), DUMPTYPE_ESTIMATE)))
#define dumptype_get_compress(dtyp)            (val_t_to_compress(dumptype_getconf((dtyp), DUMPTYPE_COMPRESS)))
#define dumptype_get_encrypt(dtyp)             (val_t_to_encrypt(dumptype_getconf((dtyp), DUMPTYPE_ENCRYPT)))
#define dumptype_get_srv_decrypt_opt(dtyp)     (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SRV_DECRYPT_OPT)))
#define dumptype_get_clnt_decrypt_opt(dtyp)    (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_CLNT_DECRYPT_OPT)))
#define dumptype_get_comprate(dtyp)            (val_t_to_rate(dumptype_getconf((dtyp), DUMPTYPE_COMPRATE)))
#define dumptype_get_tape_splitsize(dtyp)      (val_t_to_am64(dumptype_getconf((dtyp), DUMPTYPE_TAPE_SPLITSIZE)))
#define dumptype_get_fallback_splitsize(dtyp)  (val_t_to_am64(dumptype_getconf((dtyp), DUMPTYPE_FALLBACK_SPLITSIZE)))
#define dumptype_get_split_diskbuffer(dtyp)    (val_t_to_str(dumptype_getconf((dtyp), DUMPTYPE_SPLIT_DISKBUFFER)))
#define dumptype_get_record(dtyp)              (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_RECORD)))
#define dumptype_get_skip_incr(dtyp)           (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_SKIP_INCR)))
#define dumptype_get_skip_full(dtyp)           (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_SKIP_FULL)))
#define dumptype_get_to_holdingdisk(dtyp)      (val_t_to_holding(dumptype_getconf((dtyp), DUMPTYPE_HOLDINGDISK)))
#define dumptype_get_kencrypt(dtyp)            (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_KENCRYPT)))
#define dumptype_get_ignore(dtyp)              (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_IGNORE)))
#define dumptype_get_index(dtyp)               (val_t_to_boolean(dumptype_getconf((dtyp), DUMPTYPE_INDEX)))
#define dumptype_get_application(dtyp)         (val_t_to_application(dumptype_getconf((dtyp), DUMPTYPE_APPLICATION)))
#define dumptype_get_pp_scriptlist(dtyp)       (val_t_to_pp_scriptlist(dumptype_getconf((dtyp), DUMPTYPE_PP_SCRIPTLIST)))
#define dumptype_get_property(dtyp)            (val_t_to_proplist(dumptype_getconf((dtyp), DUMPTYPE_PROPERTY)))

/*
 * Interface parameter access
 */

typedef enum {
    INTER_COMMENT,
    INTER_MAXUSAGE,
    INTER_INTER /* sentinel */
} interface_key;

/* opaque object */
typedef struct interface_s interface_t;

/* Given the name of the interface, return a interface object.  Returns NULL
 * if no matching interface exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired interface
 * @returns: object or NULL
 */
interface_t *lookup_interface(char *identifier);

/* Given a interface and a key, return a pointer to the corresponding val_t.
 *
 * @param iface: the interface to examine
 * @param key: interface_key (one of the TAPETYPE_* constants)
 * @returns: pointer to value
 */
val_t *interface_getconf(interface_t *iface, interface_key key);

/* Get the name of this interface.
 *
 * @param iface: the interface to examine
 * @returns: name of the interface
 */
char *interface_name(interface_t *iface);

/* (convenience macro) has this parameter been seen in this interface?  This
 * applies to the specific parameter *within* the interface.
 *
 * @param key: interface_key
 * @returns: boolean
 */
#define interface_seen(iface, key)       (val_t_seen(interface_getconf((iface), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param iface: the interface to examine
 * @returns: various
 */
#define interface_get_comment(iface)    (val_t_to_str(interface_getconf((iface), INTER_COMMENT)))
#define interface_get_maxusage(iface)   (val_t_to_int(interface_getconf((iface), INTER_MAXUSAGE)))

/*
 * Holdingdisk parameter access
 */

typedef enum {
    HOLDING_COMMENT,
    HOLDING_DISKDIR,
    HOLDING_DISKSIZE,
    HOLDING_CHUNKSIZE,
    HOLDING_HOLDING /* sentinel */
} holdingdisk_key;

/* opaque object */
typedef struct holdingdisk_s holdingdisk_t;

/* Given the name of the holdingdisk, return a holdingdisk object.  Returns NULL
 * if no matching holdingdisk exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired holdingdisk
 * @returns: object or NULL
 */
holdingdisk_t *lookup_holdingdisk(char *identifier);

/* Return the whole linked list of holdingdisks.  Use holdingdisk_next
 * to traverse the list.
 *
 * @returns: first holding disk
 */
holdingdisk_t *getconf_holdingdisks(void);

/* Return the next holdingdisk in the list.
 *
 * @param hdisk: holding disk
 * @returns: NULL if hdisk is the last disk, otherwise the next holding
 * disk
 */
holdingdisk_t *holdingdisk_next(holdingdisk_t *hdisk);

/* Given a holdingdisk and a key, return a pointer to the corresponding val_t.
 *
 * @param hdisk: the holdingdisk to examine
 * @param key: holdingdisk_key (one of the TAPETYPE_* constants)
 * @returns: pointer to value
 */
val_t *holdingdisk_getconf(holdingdisk_t *hdisk, holdingdisk_key key);

/* Get the name of this holdingdisk.
 *
 * @param hdisk: the holdingdisk to examine
 * @returns: name of the holdingdisk
 */
char *holdingdisk_name(holdingdisk_t *hdisk);

/* (convenience macro) has this parameter been seen in this holdingdisk?  This
 * applies to the specific parameter *within* the holdingdisk.
 *
 * @param key: holdingdisk_key
 * @returns: boolean
 */
#define holdingdisk_seen(hdisk, key)       (val_t_seen(holdingdisk_getconf((hdisk), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param hdisk: the holdingdisk to examine
 * @returns: various
 */
#define holdingdisk_get_comment(hdisk)   (val_t_to_str(holdingdisk_getconf((hdisk), HOLDING_COMMENT)))
#define holdingdisk_get_diskdir(hdisk)   (val_t_to_str(holdingdisk_getconf((hdisk), HOLDING_DISKDIR)))
#define holdingdisk_get_disksize(hdisk)  (val_t_to_am64(holdingdisk_getconf((hdisk), HOLDING_DISKSIZE)))
#define holdingdisk_get_chunksize(hdisk) (val_t_to_am64(holdingdisk_getconf((hdisk), HOLDING_CHUNKSIZE)))

/* A application-tool interface */
typedef enum application_e  {
    APPLICATION_COMMENT,
    APPLICATION_PLUGIN,
    APPLICATION_PROPERTY,
    APPLICATION_APPLICATION
} application_key;

/* opaque object */
typedef struct application_s application_t;

/* Given the name of the application, return a application object.  Returns NULL
 * if no matching application exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired application
 * @returns: object or NULL
 */

application_t *lookup_application(char *identifier);

/* Given a application and a key, return a pointer to the corresponding val_t.
 *
 * @param ttyp: the application to examine
 * @param key: application (one of the APPLICATION_* constants)
 * @returns: pointer to value
 */
val_t *application_getconf(application_t *app, application_key key);

/* Get the name of this application.
 *
 * @param ttyp: the application to examine
 * @returns: name of the application
 */
char *application_name(application_t *app);

/* (convenience macro) has this parameter been seen in this application?  This
 * applies to the specific parameter *within* the application.
 *
 * @param key: application_key
 * @returns: boolean
 */
#define application_seen(app, key)       (val_t_seen(application_getconf((app), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param ttyp: the application to examine
 * @returns: various
 */
#define application_get_comment(application)  (val_t_to_str(application_getconf((application), APPLICATION_COMMENT))
#define application_get_plugin(application)   (val_t_to_str(application_getconf((application), APPLICATION_PLUGIN)))
#define application_get_property(application) (val_t_to_proplist(application_getconf((application), APPLICATION_PROPERTY)))

application_t *read_application(char *name, FILE *from, char *fname, int *linenum);

/* A pp-script-tool interface */
typedef enum pp_script_e  {
    PP_SCRIPT_COMMENT,
    PP_SCRIPT_PLUGIN,
    PP_SCRIPT_PROPERTY,
    PP_SCRIPT_EXECUTE_ON,
    PP_SCRIPT_EXECUTE_WHERE,
    PP_SCRIPT_PP_SCRIPT
} pp_script_key;

/* opaque object */
typedef struct pp_script_s pp_script_t;

/* Given the name of the pp_script, return a pp_script object.  Returns NULL
 * if no matching pp_script exists.  Note that the match is case-insensitive.
 *
 * @param identifier: name of the desired pp_script
 * @returns: object or NULL
 */

pp_script_t *lookup_pp_script(char *identifier);

/* Given a pp_script and a key, return a pointer to the corresponding val_t.
 *
 * @param ttyp: the pp_script to examine
 * @param key: pp_script (one of the PP_SCRIPT_* constants)
 * @returns: pointer to value
 */
val_t *pp_script_getconf(pp_script_t *pps, pp_script_key key);

/* Get the name of this pp_script.
 *
 * @param ttyp: the pp_script to examine
 * @returns: name of the pp_script
 */
char *pp_script_name(pp_script_t *pps);

/* (convenience macro) has this parameter been seen in this pp_script?  This
 * applies to the specific parameter *within* the pp_script.
 *
 * @param key: pp_script_key
 * @returns: boolean
 */
#define pp_script_seen(pps, key)       (val_t_seen(pp_script_getconf((pps), (key))))

/* (convenience macros)
 * fetch a particular parameter; caller must know the correct type.
 *
 * @param ttyp: the pp_script to examine
 * @returns: various
 */

#define pp_script_get_comment(pp_script)   (val_t_to_str(pp_script_getconf((pp_script), PP_SCRIPT_COMMENT)))
#define pp_script_get_plugin(pp_script)   (val_t_to_str(pp_script_getconf((pp_script), PP_SCRIPT_PLUGIN)))
#define pp_script_get_property(pp_script)   (val_t_to_proplist(pp_script_getconf((pp_script), PP_SCRIPT_PROPERTY)))
#define pp_script_get_execute_on(pp_script)   (val_t_to_execute_on(pp_script_getconf((pp_script), PP_SCRIPT_EXECUTE_ON)))
#define pp_script_get_execute_where(pp_script)   (val_t_to_execute_where(pp_script_getconf((pp_script), PP_SCRIPT_EXECUTE_WHERE)))

pp_script_t *read_pp_script(char *name, FILE *from, char *fname, int *linenum);
pp_script_t *lookup_pp_script(char *identifier);

/*
 * Command-line handling
 */

/* opaque type */
typedef struct config_overwrites_s config_overwrites_t;

/* Create a new, empty config_overwrites object.
 *
 * @param size_estimate: a guess at the number of overwrites; argc/2 is a 
 *  good estimate.
 * @returns: new object
 */
config_overwrites_t *new_config_overwrites(int size_estimate);

/* Free a config_overwrites object.  This usually won't be needed, as
 * apply_config_overwrites takes ownership of the overwrites for you.
 *
 * @param co: config_overwrites object
 */
void free_config_overwrites(config_overwrites_t *co);

/* Add an overwrite to a config_overwrites object.
 *
 * @param co: the config_overwrites object
 * @param key: the configuration parameter's key, possibly with the format
 * SUBTYPE:NAME:KEYWORD
 * @param value: the value for the parameter, as would be seen in amanda.conf
 */
void add_config_overwrite(config_overwrites_t *co,
			 char *key,
			 char *value);

/* Add an overwrite option from the command line to a config_overwrites
 * object.  Calls error() with any errors
 *
 * @param co: the config_overwrites object
 * @param optarg: the value of the command-line option
 */
void add_config_overwrite_opt(config_overwrites_t *co,
			      char *optarg);

/* Given a command line, represented as argc/argv, extract any -o options
 * as config overwrites.  This function modifies argc and argv in place.
 *
 * This is the deprecated way to extract config overwrites, for applications
 * which do not use getopt.  The preferred method is to use getopt and
 * call add_config_overwrite_opt for any -o options.
 *
 * @param argc: (in/out) command-line length
 * @param argv: (in/out) command-line strings
 * @returns: newly allocated config_overwrites object
 */
config_overwrites_t *
extract_commandline_config_overwrites(int *argc,
				      char ***argv);

/* Apply configuration overwrites to the current configuration and take
 * ownership of the config_overwrites object.
 *
 * If any parameters are not matched in the current symbol table, or
 * correspond to named subsections which do not exist, this function calls
 * error() and does not return.
 *
 * @param co: the config_overwrites object
 */
void apply_config_overwrites(config_overwrites_t *co);

/*
 * Initialization
 */

/* Constants for config_init */
typedef enum {
    /* Use arg_config_name, if not NULL */
    CONFIG_INIT_EXPLICIT_NAME = 1 << 0,

    /* Use the current working directory if an explicit name is not available */
    CONFIG_INIT_USE_CWD = 1 << 1,

    /* This is a client application (server is default) */
    CONFIG_INIT_CLIENT = 1 << 2,

    /* New configuration should "overlay" existing configuration; this
     * is used by clients to load multiple amanda-client.conf files. */
    CONFIG_INIT_OVERLAY = 1 << 3,

    /* If the file doesn't exist, halt with an error. */
    CONFIG_INIT_FATAL = 1 << 4,
} config_init_flags;

/* Initialize this application's configuration, with the specific actions
 * based on 'flags':
 *  - if CONFIG_INIT_OVERLAY is not set, configuration values are reset
 *    to their defaults
 *  - if CONFIG_INIT_EXPLICIT_NAME and arg_config_name is not NULL,
 *    use CONFIG_DIR/arg_config_name as config_dir arg_config_name as 
 *    config_name.
 *  - otherwise, if CONFIG_USE_CWD is set, use the directory in which 
 *    the application was started as config_dir, and its filename as 
 *    config_name.
 *  - otherwise, for the client only, se config_dir to CONFIG_DIR and
 *    config_name to NULL.
 *  - depending on CONFIG_INIT_CLIENT, read amanda.conf or amanda-client.conf
 *  - in the event of an error, call error() if CONFIG_INIT_FATAL, otherwise
 *    record a message in the debug log and return false.
 *
 * @param flags: flags indicating desired behavior, as above
 * @param arg_config_name: config name to use (from e.g., argv[1])
 * @returns: true on success, false on failure, unless CONFIG_INIT_FATAL
 */
gboolean config_init(config_init_flags flags,
		     char *arg_config_name);

/* Free all memory allocated for the configuration.  This effectively
 * reverses the effects of config_init().
 */
void config_uninit(void);

/* Encode any applied config_overwrites into a strv format suitale for
 * executing another Amanda tool.
 *
 * The * result is dynamically allocated and NULL terminated.  There is no
 * provision to free the result, as this function is always called just
 * before execve(..).
 *
 * First gives the number of array elements to leave for the caller to
 * fill in.  The usual calling pattern is this:
 *   command_line = get_config_options(3);
 *   command_line[0] = "appname";
 *   command_line[1] = config_name;
 *   command_line[2] = "--foo";
 *   execve(command_line[0], command_line, safe_env());
 *
 * @param first: number of unused elements to leave at the beginning of
 * the array.
 * @returns: NULL-terminated string array suitable for execve
 */
char **get_config_options(int first);

/* Get the config name */
char *get_config_name(void);

/* Get the config directory */
char *get_config_dir(void);

/* Get the config filename */
char *get_config_filename(void);

/*
 * Utilities
 */

/* Security plugins get their configuration information through a callback
 * with the signature:
 *   char *callback(char *key, void *userpointer);
 * where key is the name of the desired parameter, which may not match the
 * name used in this module.  See the implementations of these functions
 * to learn which keys they support, or to add new keys.
 */
char *generic_client_get_security_conf(char *, void *);
char *generic_get_security_conf(char *, void *);

/* Dump the current configuration information to stdout, in a format 
 * that can be re-read by this module.  The results will include any
 * command-line overwrites.
 *
 * This function only dumps the server configuration, and will fail on
 * clients.
 */
void dump_configuration(void);

/* Return a sequence of strings giving the printable representation
 * of the given val_t.  If str_needs_quotes is true and each string is
 * prefixed by the relevant configuration keyword, these strings will
 * be parseable by this module, and will reproduce exactly the same
 * configuration value.  See the implementation of dump_configuration
 * for details.
 *
 * If str_needs_quotes is provided, a CONFTYPE_STR value will be returned with 
 * quotes.
 *
 * The result is a NULL-terminated strv, which can be freed with g_strfreev or
 * joined with g_strjoinv.  Caller is responsible for freeing the memory.
 *
 * @param val: the value to analyze
 * @param str_needs_quotes: add quotes to CONFTYPE_STR values?
 * @returns: NULL-terminated string vector
 */
char **val_t_display_strs(val_t *val, int str_needs_quotes);

/* Read a dumptype; this is used by this module as well as by diskfile.c to
 * read the disklist.  The two are carefully balanced in their parsing process.
 *
 * Nobody else should use this function.  Seriously.
 */
dumptype_t *read_dumptype(char *name, FILE *from, char *fname, int *linenum);

/* Extend a relative filename with the current config_dir; if filename is already
 * absolute, this is equivalent to stralloc.
 *
 * @param filename: filename to extend
 * @returns: newly allocated filename
 */
char *config_dir_relative(char *filename);

/* Convert from a symbol back to a name for logging and for dumping
 * config values
 *
 * @param taperalgo: the constant value
 * @returns: statically allocated string
 */
char *taperalgo2str(taperalgo_t taperalgo);

/* Looks for a unit value like b, byte, bytes, bps, etc. Technically
 * the return value should never be < 1, but we return a signed value
 * to help mitigate bad C promotion semantics. Returns 0 on error.
 *
 * This is here in this module because it uses the numb_keytable.
 *
 * @param casestr: the unit string
 * @returns: the corresponding multiplier (e.g., 'M' => 1024*1024)
 */
gint64 find_multiplier(char * casestr);

#endif /* ! CONFFILE_H */
