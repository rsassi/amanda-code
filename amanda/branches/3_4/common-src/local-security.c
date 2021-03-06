/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland
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
 * $Id: local-security.c 6512 2007-05-24 17:00:24Z ian $
 *
 * local-security.c - security and transport over local or a local-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that local might want to spew out.
 */

#include "amanda.h"
#include "match.h"
#include "amutil.h"
#include "event.h"
#include "packet.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"

/*
 * Number of seconds amandad has to start up
 */
#define CONNECT_TIMEOUT 20

/*
 * Interface functions
 */
static void local_connect(const char *, char *(*)(char *, void *),
			void (*)(void *, security_handle_t *, security_status_t),
			void *, void *);

static char *local_get_authenticated_peer_name_hostname(security_handle_t *hdl);

static gboolean is_local(const char *);

/*
 * This is our interface to the outside world.
 */
const security_driver_t local_security_driver = {
    "LOCAL",
    local_connect,
    sec_accept,
    local_get_authenticated_peer_name_hostname,
    sec_close,
    stream_sendpkt,
    stream_recvpkt,
    stream_recvpkt_cancel,
    tcpma_stream_server,
    tcpma_stream_accept,
    tcpma_stream_client,
    tcpma_stream_close,
    tcpma_stream_close_async,
    sec_stream_auth,
    sec_stream_id,
    tcpm_stream_write,
    tcpm_stream_write_async,
    tcpm_stream_read,
    tcpm_stream_read_sync,
    tcpm_stream_read_to_shm_ring,
    tcpm_stream_read_cancel,
    tcpm_stream_pause,
    tcpm_stream_resume,
    tcpm_close_connection,
    NULL,
    NULL,
    generic_data_write,
    generic_data_write_non_blocking,
    generic_data_read
};

static int newhandle = 1;

/*
 * Local functions
 */
static int runlocal(struct tcp_conn *, const char *, const char *);


/*
 * local version of a security handle allocator.  Logically sets
 * up a network "connection".
 */
static void
local_connect(
    const char *	hostname,
    char *		(*conf_fn)(char *, void *),
    void		(*fn)(void *, security_handle_t *, security_status_t),
    void *		arg,
    void *		datap)
{
    struct sec_handle *rh;
    char *amandad_path=NULL;
    char *client_username=NULL;

    assert(fn != NULL);
    assert(hostname != NULL);

    auth_debug(1, _("local: local_connect: %s\n"), hostname);

    rh = g_new0(struct sec_handle, 1);
    security_handleinit(&rh->sech, &local_security_driver);
    rh->dle_hostname = g_strdup(hostname);
    rh->hostname = NULL;
    rh->rs = NULL;
    rh->ev_timeout = NULL;
    rh->rc = NULL;

    if (!is_local(hostname)) {
	security_seterror(&rh->sech,
	    _("%s: is not local"), hostname);
	(*fn)(arg, &rh->sech, S_ERROR);
	return;
    }
    rh->hostname = g_strdup(hostname);
    rh->rs = tcpma_stream_client(rh, newhandle++);
    if (rh->rc == NULL)
	goto error;

    if (rh->rs == NULL)
	goto error;

    amfree(rh->hostname);
    rh->hostname = g_strdup(rh->rs->rc->hostname);

    /*
     * We need to open a new connection.
     *
     * XXX need to eventually limit number of outgoing connections here.
     */
    if(conf_fn) {
	amandad_path    = conf_fn("amandad_path", datap);
	client_username = conf_fn("client_username", datap);
    }
    if(rh->rc->read == -1) {
	if (runlocal(rh->rs->rc, amandad_path, client_username) < 0) {
	    security_seterror(&rh->sech, _("can't connect to %s: %s"),
			      hostname, rh->rs->rc->errmsg);
	    goto error;
	}
	rh->rc->refcnt++;
    }

    /*
     * The socket will be opened async so hosts that are down won't
     * block everything.  We need to register a write event
     * so we will know when the socket comes alive.
     *
     * Overload rh->rs->ev_read to provide a write event handle.
     * We also register a timeout.
     */
    rh->fn.connect = fn;
    rh->arg = arg;
    rh->rs->rc->ev_write = event_create((event_id_t)rh->rs->rc->write, EV_WRITEFD,
	sec_connect_callback, rh);
    rh->ev_timeout = event_create((event_id_t)CONNECT_TIMEOUT, EV_TIME,
	sec_connect_timeout, rh);
    event_activate(rh->rs->rc->ev_write);
    event_activate(rh->ev_timeout);

    return;

error:
    (*fn)(arg, &rh->sech, S_ERROR);
    amfree(rh->hostname);
}

/*
 * Forks a local to the host listed in rc->hostname
 * Returns negative on error, with an errmsg in rc->errmsg.
 */
static int
runlocal(
    struct tcp_conn *	rc,
    const char *	amandad_path,
    const char *	client_username G_GNUC_UNUSED)
{
    int rpipe[2], wpipe[2];
    char *xamandad_path = (char *)amandad_path;

#ifndef SINGLE_USERID
    struct passwd *pwd = NULL;
    uid_t uid = 0;
    gid_t gid = 0;

    if (getuid() == 0) {
	if (client_username && strlen(client_username) > 1) {
	    pwd = getpwnam(client_username);
            if (!pwd) {
		dbprintf("User '%s' doesn't exist\n", client_username);
	    } else {
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
	    }
	}
	if (!pwd) {
	    uid = get_client_uid();
	    gid = get_client_gid();
	}
    }
#endif

    memset(rpipe, -1, sizeof(rpipe));
    memset(wpipe, -1, sizeof(wpipe));
    if (pipe(rpipe) < 0 || pipe(wpipe) < 0) {
	g_free(rc->errmsg);
	rc->errmsg = g_strdup_printf(_("pipe: %s"), strerror(errno));
	return (-1);
    }

    switch (rc->pid = fork()) {
    case -1:
	g_free(rc->errmsg);
	rc->errmsg = g_strdup_printf(_("fork: %s"), strerror(errno));
	aclose(rpipe[0]);
	aclose(rpipe[1]);
	aclose(wpipe[0]);
	aclose(wpipe[1]);
	return (-1);
    case 0:
	aclose(wpipe[1]);
	aclose(rpipe[0]);
	close(0);
	close(1);
	dup2(wpipe[0], 0);
	dup2(rpipe[1], 1);
	aclose(wpipe[0]);
	aclose(rpipe[1]);
	break;
    default:
	rc->read = rpipe[0];
	aclose(rpipe[1]);
	rc->write = wpipe[1];
	aclose(wpipe[0]);
	return (0);
    }

    /* drop root privs for good */
    set_root_privs(-1);

    if(!xamandad_path || strlen(xamandad_path) <= 1)
	xamandad_path = g_strjoin(NULL, amlibexecdir, "/", "amandad", NULL);

#ifndef SINGLE_USERID
    if (client_username && *client_username != '\0') {
	initgroups(client_username, gid);
    } else {
	initgroups(CLIENT_LOGIN, gid);
    }
    if (gid != 0) {
	if (setregid(gid, gid) == -1) {
	    error("Can't setregid(%d,%d): %s", gid, gid, strerror(errno));
	}
    }
    if (uid != 0) {
	if (setreuid(uid, uid) == -1) {
	    error("Can't setreuid(%d,%d): %s", uid, uid, strerror(errno));
	}
    }
#endif

    safe_fd(-1, 0);

    execlp(xamandad_path, xamandad_path,
	   "-auth=local", (char *)NULL);
    error(_("error: couldn't exec %s: %s"), xamandad_path, strerror(errno));

    /* should never go here, shut up compiler warning */
    return(-1);
}

static char *
local_get_authenticated_peer_name_hostname(
    security_handle_t *hdl G_GNUC_UNUSED)
{
    char *server_hostname;
    server_hostname = malloc(1024);
    if (gethostname(server_hostname, 1024) == 0) {
	server_hostname[1023] = '\0';
	return server_hostname;
    }
    amfree(server_hostname);
    return g_strdup("localhost");

}

/*
 * Recognize whether hostname refers to this host, regardless of how it is
 * spelled, capitalized, abbreviated or fully qualified, etc.
 *
 * If there exists any address for hostname to which a socket can be bound,
 * return TRUE.
 */
static gboolean
is_local(const char *hostname)
{
    struct addrinfo *addrs;
    struct addrinfo *a;
    int rslt;
    int s;
    gboolean verdict = FALSE;

    rslt = resolve_hostname(hostname, SOCK_STREAM, &addrs, NULL);
    if (0 != rslt) {
        dbprintf("Unresolved hostname %s assumed nonlocal: %s\n",
	         hostname, gai_strerror(rslt));
        return verdict;
    }
    /* Beyond this point, addrs must be freed */

    /* Invariant: s is not an open socket */
    for ( a = addrs ; NULL != a ; a = a->ai_next ) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
	if ( -1 == s )
	    continue;
	rslt = bind(s, a->ai_addr, a->ai_addrlen);
	if ( 0 == rslt ) {
	    close(s);
	    verdict = TRUE;
	    break;
	}
	if ( EADDRNOTAVAIL != errno ) {
	    dbprintf("strange bind() result %s\n", strerror(errno));
	}
	close(s);
    }
    /* s is not open here */

    freeaddrinfo(addrs);
    return verdict;
}
