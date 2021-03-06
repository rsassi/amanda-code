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
 * $Id: security-util.c,v 1.25 2006/07/22 12:04:47 martinea Exp $
 *
 * sec-security.c - security and transport over sec or a sec-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that sec might want to spew out.
 */

#include "amanda.h"
#include "util.h"
#include "event.h"
#include "packet.h"
#include "queue.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"
#include "version.h"

/*
 * Magic values for sec_conn->handle
 */
#define	H_TAKEN	-1		/* sec_conn->tok was already read */
#define	H_EOF	-2		/* this connection has been shut down */

/*
 * This is a queue of open connections
 */
struct connq_s connq = {
    TAILQ_HEAD_INITIALIZER(connq.tailq), 0
};
static int newhandle = 1;
static int newevent = 1;

/*
 * Local functions
 */
static void recvpkt_callback(void *, void *, ssize_t);
static void stream_read_callback(void *);
static void stream_read_sync_callback(void *);

static void sec_tcp_conn_read_cancel(struct tcp_conn *);
static void sec_tcp_conn_read_callback(void *);


/*
 * Authenticate a stream
 * Nothing needed for sec.  The connection is authenticated by secd
 * on startup.
 */
int
sec_stream_auth(
    void *	s)
{
    (void)s;	/* Quiet unused parameter warning */
    return (0);
}

/*
 * Returns the stream id for this stream.  This is just the local
 * port.
 */
int
sec_stream_id(
    void *	s)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    return (rs->handle);
}

/*
 * Setup to handle new incoming connections
 */
void
sec_accept(
    const security_driver_t *driver,
    int		in,
    int		out,
    void	(*fn)(security_handle_t *, pkt_t *))
{
    struct tcp_conn *rc;

    rc = sec_tcp_conn_get("unknown",0);
    rc->read = in;
    rc->write = out;
    rc->accept_fn = fn;
    rc->driver = driver;
    sec_tcp_conn_read(rc);
}

/*
 * frees a handle allocated by the above
 */
void
sec_close(
    void *	inst)
{
    struct sec_handle *rh = inst;

    assert(rh != NULL);

    auth_debug(1, ("%s: sec: closing handle to %s\n", debug_prefix_time(NULL),
		   rh->hostname));

    if (rh->rs != NULL) {
	/* This may be null if we get here on an error */
	stream_recvpkt_cancel(rh);
	security_stream_close(&rh->rs->secstr);
    }
    /* keep us from getting here again */
    rh->sech.driver = NULL;
    amfree(rh->hostname);
    amfree(rh);
}

/*
 * Called when a sec connection is finished connecting and is ready
 * to be authenticated.
 */
void
sec_connect_callback(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    event_release(rh->rs->ev_read);
    rh->rs->ev_read = NULL;
    event_release(rh->ev_timeout);
    rh->ev_timeout = NULL;

    (*rh->fn.connect)(rh->arg, &rh->sech, S_OK);
}

/*
 * Called if a connection times out before completion.
 */
void
sec_connect_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    event_release(rh->rs->ev_read);
    rh->rs->ev_read = NULL;
    event_release(rh->ev_timeout);
    rh->ev_timeout = NULL;

    (*rh->fn.connect)(rh->arg, &rh->sech, S_TIMEOUT);
}

void
sec_close_connection_none(
    void *h,
    char *hostname)
{
    h = h;
    hostname = hostname;

    return;
}



/*
 * Transmit a packet.
 */
ssize_t
stream_sendpkt(
    void *	cookie,
    pkt_t *	pkt)
{
    char *buf;
    struct sec_handle *rh = cookie;
    size_t len;
    char *s;

    assert(rh != NULL);
    assert(pkt != NULL);

    auth_debug(1, ("%s: sec: stream_sendpkt: enter\n",
		   debug_prefix_time(NULL)));

    if (rh->rc->prefix_packet)
	s = rh->rc->prefix_packet(rh, pkt);
    else
	s = "";
    len = strlen(pkt->body) + strlen(s) + 2;
    buf = alloc(len);
    buf[0] = (char)pkt->type;
    strncpy(&buf[1], s, len - 1);
    strncpy(&buf[1 + strlen(s)], pkt->body, (len - strlen(s) - 1));
    if (strlen(s) > 0)
	amfree(s);

    auth_debug(1,
     ("%s: sec: stream_sendpkt: %s (%d) pkt_t (len " SIZE_T_FMT ") contains:\n\n\"%s\"\n\n",
      debug_prefix_time(NULL), pkt_type2str(pkt->type), pkt->type,
      strlen(pkt->body), pkt->body));

    if (security_stream_write(&rh->rs->secstr, buf, len) < 0) {
	security_seterror(&rh->sech, security_stream_geterror(&rh->rs->secstr));
	return (-1);
    }
    amfree(buf);
    return (0);
}

/*
 * Set up to receive a packet asyncronously, and call back when
 * it has been read.
 */
void
stream_recvpkt(
    void *	cookie,
    void	(*fn)(void *, pkt_t *, security_status_t),
    void *	arg,
    int		timeout)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, ("%s: sec: recvpkt registered for %s\n",
		   debug_prefix_time(NULL), rh->hostname));

    /*
     * Reset any pending timeout on this handle
     */
    if (rh->ev_timeout != NULL)
	event_release(rh->ev_timeout);

    /*
     * Negative timeouts mean no timeout
     */
    if (timeout < 0) {
	rh->ev_timeout = NULL;
    } else {
	rh->ev_timeout = event_register((event_id_t)timeout, EV_TIME,
		stream_recvpkt_timeout, rh);
    }
    rh->fn.recvpkt = fn;
    rh->arg = arg;
    security_stream_read(&rh->rs->secstr, recvpkt_callback, rh);
}

/*
 * This is called when a handle times out before receiving a packet.
 */
void
stream_recvpkt_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, ("%s: sec: recvpkt timeout for %s\n",
		   debug_prefix_time(NULL), rh->hostname));

    stream_recvpkt_cancel(rh);
    (*rh->fn.recvpkt)(rh->arg, NULL, S_TIMEOUT);
}

/*
 * Remove a async receive request from the queue
 */
void
stream_recvpkt_cancel(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    auth_debug(1, ("%s: sec: cancelling recvpkt for %s\n",
		   debug_prefix_time(NULL), rh->hostname));

    assert(rh != NULL);

    security_stream_read_cancel(&rh->rs->secstr);
    if (rh->ev_timeout != NULL) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
int
tcpm_stream_write(
    void *	s,
    const void *buf,
    size_t	size)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);
    assert(rs->rc != NULL);

    auth_debug(1, ("%s: sec: stream_write: writing " SIZE_T_FMT " bytes to %s:%d %d\n",
		   debug_prefix_time(NULL), size, rs->rc->hostname, rs->handle,
		   rs->rc->write));

    if (tcpm_send_token(rs->rc, rs->rc->write, rs->handle, &rs->rc->errmsg,
			     buf, size)) {
	security_stream_seterror(&rs->secstr, rs->rc->errmsg);
	return (-1);
    }
    return (0);
}

/*
 * Submit a request to read some data.  Calls back with the given
 * function and arg when completed.
 */
void
tcpm_stream_read(
    void *	s,
    void	(*fn)(void *, void *, ssize_t),
    void *	arg)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    if (rs->ev_read == NULL) {
	rs->ev_read = event_register((event_id_t)rs->rc, EV_WAIT,
	    stream_read_callback, rs);
	sec_tcp_conn_read(rs->rc);
    }
    rs->fn = fn;
    rs->arg = arg;
}

/*
 * Write a chunk of data to a stream.  Blocks until completion.
 */
ssize_t
tcpm_stream_read_sync(
    void *	s,
    void **	buf)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    /*
     * Only one read request can be active per stream.
     */
    if (rs->ev_read != NULL) {
	return -1;
    }
    rs->ev_read = event_register((event_id_t)rs->rc, EV_WAIT,
        stream_read_sync_callback, rs);
    sec_tcp_conn_read(rs->rc);
    event_wait(rs->ev_read);
    *buf = rs->rc->pkt;
    return (rs->rc->pktlen);
}

/*
 * Cancel a previous stream read request.  It's ok if we didn't have a read
 * scheduled.
 */
void
tcpm_stream_read_cancel(
    void *	s)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    if (rs->ev_read != NULL) {
	event_release(rs->ev_read);
	rs->ev_read = NULL;
	sec_tcp_conn_read_cancel(rs->rc);
    }
}

/*
 * Transmits a chunk of data over a rsh_handle, adding
 * the necessary headers to allow the remote end to decode it.
 */
ssize_t
tcpm_send_token(
    struct tcp_conn *rc,
    int		fd,
    int		handle,
    char **	errmsg,
    const void *buf,
    size_t	len)
{
    uint32_t		nethandle;
    uint32_t		netlength;
    struct iovec	iov[3];
    int			nb_iov = 3;
    int			rval;
    char		*encbuf;
    ssize_t		encsize;

    assert(SIZEOF(netlength) == 4);

    auth_debug(1, ("%s: tcpm_send_token: write %zd bytes to handle %d\n",
	  debug_prefix_time(NULL), len, handle));
    /*
     * Format is:
     *   32 bit length (network byte order)
     *   32 bit handle (network byte order)
     *   data
     */
    netlength = htonl(len);
    iov[0].iov_base = (void *)&netlength;
    iov[0].iov_len = SIZEOF(netlength);

    nethandle = htonl((uint32_t)handle);
    iov[1].iov_base = (void *)&nethandle;
    iov[1].iov_len = SIZEOF(nethandle);

    encbuf = (char *)buf;
    encsize = len;

    if(len == 0) {
	nb_iov = 2;
    }
    else {
	if (rc->driver->data_encrypt == NULL) {
	    iov[2].iov_base = (void *)buf;
	    iov[2].iov_len = len;
	} else {
	    /* (the extra (void *) cast is to quiet type-punning warnings) */
	    rc->driver->data_encrypt(rc, (void *)buf, len, (void **)(void *)&encbuf, &encsize);
	    iov[2].iov_base = (void *)encbuf;
	    iov[2].iov_len = encsize;
	    netlength = htonl(encsize);
	}
        nb_iov = 3;
    }

    rval = net_writev(fd, iov, nb_iov);
    if (len != 0 && rc->driver->data_encrypt != NULL && buf != encbuf) {
	amfree(encbuf);
    }

    if (rval < 0) {
	if (errmsg)
            *errmsg = newvstralloc(*errmsg, "write error to ",
				   ": ", strerror(errno), NULL);
        return (-1);
    }
    return (0);
}

/*
 *  return -1 on error
 *  return  0 on EOF:   *handle = H_EOF  && *size = 0    if socket closed
 *  return  0 on EOF:   *handle = handle && *size = 0    if stream closed
 *  return size     :   *handle = handle && *size = size for data read
 */

ssize_t
tcpm_recv_token(
    struct tcp_conn    *rc,
    int		fd,
    int *	handle,
    char **	errmsg,
    char **	buf,
    ssize_t *	size,
    int		timeout)
{
    unsigned int netint[2];

    assert(SIZEOF(netint) == 8);

    switch (net_read(fd, &netint, SIZEOF(netint), timeout)) {
    case -1:
	if (errmsg)
	    *errmsg = newvstralloc(*errmsg, "recv error: ", strerror(errno),
				   NULL);
	auth_debug(1, ("%s: tcpm_recv_token: A return(-1)\n",
		       debug_prefix_time(NULL)));
	return (-1);
    case 0:
	*size = 0;
	*handle = H_EOF;
	*errmsg = newvstralloc(*errmsg, "SOCKET_EOF", NULL);
	auth_debug(1, ("%s: tcpm_recv_token: A return(0)\n",
		       debug_prefix_time(NULL)));
	return (0);
    default:
	break;
    }

    *size = (ssize_t)ntohl(netint[0]);
    *handle = (int)ntohl(netint[1]);
    /* amanda protocol packet can be above NETWORK_BLOCK_BYTES */
    if (*size > 128*NETWORK_BLOCK_BYTES || *size < 0) {
	if (isprint((int)(*size        ) & 0xFF) &&
	    isprint((int)(*size   >> 8 ) & 0xFF) &&
	    isprint((int)(*size   >> 16) & 0xFF) &&
	    isprint((int)(*size   >> 24) & 0xFF) &&
	    isprint((*handle      ) & 0xFF) &&
	    isprint((*handle >> 8 ) & 0xFF) &&
	    isprint((*handle >> 16) & 0xFF) &&
	    isprint((*handle >> 24) & 0xFF)) {
	    char s[101];
	    int i;
	    s[0] = ((int)(*size)  >> 24) & 0xFF;
	    s[1] = ((int)(*size)  >> 16) & 0xFF;
	    s[2] = ((int)(*size)  >>  8) & 0xFF;
	    s[3] = ((int)(*size)       ) & 0xFF;
	    s[4] = (*handle >> 24) & 0xFF;
	    s[5] = (*handle >> 16) & 0xFF;
	    s[6] = (*handle >> 8 ) & 0xFF;
	    s[7] = (*handle      ) & 0xFF;
	    i = 8; s[i] = ' ';
	    while(i<100 && isprint(s[i]) && s[i] != '\n') {
		switch(net_read(fd, &s[i], 1, 0)) {
		case -1: s[i] = '\0'; break;
		case  0: s[i] = '\0'; break;
		default: dbprintf(("read: %c\n", s[i])); i++; s[i]=' ';break;
		}
	    }
	    s[i] = '\0';
	    *errmsg = newvstralloc(*errmsg, "tcpm_recv_token: invalid size: ",
				   s, NULL);
	    dbprintf(("%s: tcpm_recv_token: invalid size: %s\n",
		      debug_prefix_time(NULL), s));
	} else {
	    *errmsg = newvstralloc(*errmsg, "tcpm_recv_token: invalid size",
				   NULL);
	    dbprintf(("%s: tcpm_recv_token: invalid size " SSIZE_T_FMT "\n",
		      debug_prefix_time(NULL), *size));
	}
	*size = -1;
	return -1;
    }
    amfree(*buf);
    *buf = alloc((size_t)*size);

    if(*size == 0) {
	auth_debug(1, ("%s: tcpm_recv_token: read EOF from %d\n",
		       debug_prefix_time(NULL), *handle));
	*errmsg = newvstralloc(*errmsg, "EOF",
				   NULL);
	return 0;
    }
    switch (net_read(fd, *buf, (size_t)*size, timeout)) {
    case -1:
	if (errmsg)
	    *errmsg = newvstralloc(*errmsg, "recv error: ", strerror(errno),
				   NULL);
	auth_debug(1, ("%s: tcpm_recv_token: B return(-1)\n",
		       debug_prefix_time(NULL)));
	return (-1);
    case 0:
	*size = 0;
	*errmsg = newvstralloc(*errmsg, "SOCKET_EOF", NULL);
	auth_debug(1, ("%s: tcpm_recv_token: B return(0)\n",
		       debug_prefix_time(NULL)));
	return (0);
    default:
	break;
    }

    auth_debug(1, ("%s: tcpm_recv_token: read " SSIZE_T_FMT " bytes from %d\n",
		   debug_prefix_time(NULL), *size, *handle));

    if (*size > 0 && rc->driver->data_decrypt != NULL) {
	char *decbuf;
	ssize_t decsize;
	/* (the extra (void *) cast is to quiet type-punning warnings) */
	rc->driver->data_decrypt(rc, *buf, *size, (void **)(void *)&decbuf, &decsize);
	if (*buf != decbuf) {
	    amfree(*buf);
	    *buf = decbuf;
	}
	*size = decsize;
    }

    return((*size));
}

void
tcpm_close_connection(
    void *h,
    char *hostname)
{
    struct sec_handle *rh = h;

    (void)hostname;

    if (rh && rh->rc && rh->rc->toclose == 0) {
	rh->rc->toclose = 1;
	sec_tcp_conn_put(rh->rc);
    }
}



/*
 * Accept an incoming connection on a stream_server socket
 * Nothing needed for tcpma.
 */
int
tcpma_stream_accept(
    void *	s)
{
    (void)s;	/* Quiet unused parameter warning */

    return (0);
}

/*
 * Return a connected stream.  For sec, this means setup a stream
 * with the supplied handle.
 */
void *
tcpma_stream_client(
    void *	h,
    int		id)
{
    struct sec_handle *rh = h;
    struct sec_stream *rs;

    assert(rh != NULL);

    if (id <= 0) {
	security_seterror(&rh->sech,
	    "%d: invalid security stream id", id);
	return (NULL);
    }

    rs = alloc(SIZEOF(*rs));
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->handle = id;
    rs->ev_read = NULL;
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rh->rc->refcnt++;
    }
    else {
	rs->rc = sec_tcp_conn_get(rh->hostname, 0);
	rs->rc->driver = rh->sech.driver;
	rh->rc = rs->rc;
    }

    auth_debug(1, ("%s: sec: stream_client: connected to stream %d\n",
		   debug_prefix_time(NULL), id));

    return (rs);
}

/*
 * Create the server end of a stream.  For sec, this means setup a stream
 * object and allocate a new handle for it.
 */
void *
tcpma_stream_server(
    void *	h)
{
    struct sec_handle *rh = h;
    struct sec_stream *rs;

    assert(rh != NULL);

    rs = alloc(SIZEOF(*rs));
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rs->rc->refcnt++;
    }
    else {
	rs->rc = sec_tcp_conn_get(rh->hostname, 0);
	rs->rc->driver = rh->sech.driver;
	rh->rc = rs->rc;
    }
    /*
     * Stream should already be setup!
     */
    if (rs->rc->read < 0) {
	sec_tcp_conn_put(rs->rc);
	amfree(rs);
	security_seterror(&rh->sech, "lost connection to %s", rh->hostname);
	return (NULL);
    }
    assert(strcmp(rh->hostname, rs->rc->hostname) == 0);
    /*
     * so as not to conflict with the amanda server's handle numbers,
     * we start at 500000 and work down
     */
    rs->handle = 500000 - newhandle++;
    rs->ev_read = NULL;
    auth_debug(1, ("%s: sec: stream_server: created stream %d\n",
		   debug_prefix_time(NULL), rs->handle));
    return (rs);
}

/*
 * Close and unallocate resources for a stream.
 */
void
tcpma_stream_close(
    void *	s)
{
    struct sec_stream *rs = s;
    char buf = 0;

    assert(rs != NULL);

    auth_debug(1, ("%s: sec: tcpma_stream_close: closing stream %d\n",
		   debug_prefix_time(NULL), rs->handle));

    if(rs->closed_by_network == 0 && rs->rc->write != -1)
	tcpm_stream_write(rs, &buf, 0);
    security_stream_read_cancel(&rs->secstr);
    if(rs->closed_by_network == 0)
	sec_tcp_conn_put(rs->rc);
    amfree(rs);
}

/*
 * Create the server end of a stream.  For bsdudp, this means setup a tcp
 * socket for receiving a connection.
 */
void *
tcp1_stream_server(
    void *	h)
{
    struct sec_stream *rs = NULL;
    struct sec_handle *rh = h;

    assert(rh != NULL);

    rs = alloc(SIZEOF(*rs));
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rs->handle = 500000 - newhandle++;
	rs->rc->refcnt++;
	rs->socket = 0;		/* the socket is already opened */
    }
    else {
	rh->rc = sec_tcp_conn_get(rh->hostname, 1);
	rh->rc->driver = rh->sech.driver;
	rs->rc = rh->rc;
	rs->socket = stream_server(&rs->port, STREAM_BUFSIZE, 
		STREAM_BUFSIZE, 0);
	if (rs->socket < 0) {
	    security_seterror(&rh->sech,
			    "can't create server stream: %s", strerror(errno));
	    amfree(rs);
	    return (NULL);
	}
	rh->rc->read = rs->socket;
	rh->rc->write = rs->socket;
	rs->handle = (int)rs->port;
    }
    rs->fd = -1;
    rs->ev_read = NULL;
    return (rs);
}

/*
 * Accepts a new connection on unconnected streams.  Assumes it is ok to
 * block on accept()
 */
int
tcp1_stream_accept(
    void *	s)
{
    struct sec_stream *bs = s;

    assert(bs != NULL);
    assert(bs->socket != -1);
    assert(bs->fd < 0);

    if (bs->socket > 0) {
	bs->fd = stream_accept(bs->socket, 30, STREAM_BUFSIZE, STREAM_BUFSIZE);
	if (bs->fd < 0) {
	    security_stream_seterror(&bs->secstr,
				     "can't accept new stream connection: %s",
				     strerror(errno));
	    return (-1);
	}
	bs->rc->read = bs->fd;
	bs->rc->write = bs->fd;
    }
    return (0);
}

/*
 * Return a connected stream
 */
void *
tcp1_stream_client(
    void *	h,
    int		id)
{
    struct sec_stream *rs = NULL;
    struct sec_handle *rh = h;

    assert(rh != NULL);

    rs = alloc(SIZEOF(*rs));
    security_streaminit(&rs->secstr, rh->sech.driver);
    rs->handle = id;
    rs->ev_read = NULL;
    rs->closed_by_me = 0;
    rs->closed_by_network = 0;
    if (rh->rc) {
	rs->rc = rh->rc;
	rh->rc->refcnt++;
    }
    else {
	rh->rc = sec_tcp_conn_get(rh->hostname, 1);
	rh->rc->driver = rh->sech.driver;
	rs->rc = rh->rc;
	rh->rc->read = stream_client(rh->hostname, (in_port_t)id,
			STREAM_BUFSIZE, STREAM_BUFSIZE, &rs->port, 0);
	if (rh->rc->read < 0) {
	    security_seterror(&rh->sech,
			      "can't connect stream to %s port %d: %s",
			       rh->hostname, id, strerror(errno));
	    amfree(rs);
	    return (NULL);
        }
	rh->rc->write = rh->rc->read;
    }
    rs->socket = -1;	/* we're a client */
    rh->rs = rs;
    return (rs);
}

int
tcp_stream_write(
    void *	s,
    const void *buf,
    size_t	size)
{
    struct sec_stream *rs = s;

    assert(rs != NULL);

    if (fullwrite(rs->fd, buf, size) < 0) {
        security_stream_seterror(&rs->secstr,
            "write error on stream %d: %s", rs->port, strerror(errno));
        return (-1);
    }
    return (0);
}

char *
bsd_prefix_packet(
    void *	h,
    pkt_t *	pkt)
{
    struct sec_handle *rh = h;
    struct passwd *pwd;
    char *buf;

    if (pkt->type != P_REQ)
	return "";

    if ((pwd = getpwuid(getuid())) == NULL) {
	security_seterror(&rh->sech,
			  "can't get login name for my uid %ld",
			  (long)getuid());
	return(NULL);
    }
    buf = alloc(16+strlen(pwd->pw_name));
    strncpy(buf, "SECURITY USER ", (16 + strlen(pwd->pw_name)));
    strncpy(&buf[14], pwd->pw_name, (16 + strlen(pwd->pw_name) - 14));
    buf[14 + strlen(pwd->pw_name)] = '\n';
    buf[15 + strlen(pwd->pw_name)] = '\0';

    return (buf);
}


/*
 * Check the security of a received packet.  Returns negative on security
 * violation, or returns 0 if ok.  Removes the security info from the pkt_t.
 */
int
bsd_recv_security_ok(
    struct sec_handle *	rh,
    pkt_t *		pkt)
{
    char *tok, *security, *body, *result;
    char *service = NULL, *serviceX, *serviceY;
    char *security_line;
    char *s, ch;
    size_t len;
    in_port_t port;

    /*
     * Now, find the SECURITY line in the body, and parse it out
     * into an argv.
     */
    if (strncmp_const(pkt->body, "SECURITY ") == 0) {
	security = pkt->body;
	len = 0;
	while(*security != '\n' && len < pkt->size) {
	    security++;
	    len++;
	}
	if(*security == '\n') {
	    body = security+1;
	    *security = '\0';
	    security_line = stralloc(pkt->body);
	    security = pkt->body + strlen("SECURITY ");
	} else {
	    body = pkt->body;
	    security_line = NULL;
	    security = NULL;
	}
    } else {
	body = pkt->body;
	security_line = NULL;
	security = NULL;
    }

    /*
     * Now, find the SERVICE line in the body, and parse it out
     * into an argv.
     */
    s = body;
    if (strncmp_const_skip(s, "SERVICE ", s, ch) == 0) {
	serviceX = stralloc(s);
	serviceY = strtok(serviceX, "\n");
	if (serviceY)
	    service  = stralloc(serviceY);
	amfree(serviceX);
    }

    /*
     * We need to do different things depending on which type of packet
     * this is.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Request packets must come from a reserved port
	 */
    port = SS_GET_PORT(&rh->peer);
	if (port >= IPPORT_RESERVED) {
	    security_seterror(&rh->sech,
		"host %s: port %u not secure", rh->hostname,
		(unsigned int)port);
	    amfree(service);
	    amfree(security_line);
	    return (-1);
	}

	if (!service) {
	    security_seterror(&rh->sech,
			      "packet as no SERVICE line");
	    amfree(security_line);
	    return (-1);
	}

	/*
	 * Request packets contain a remote username.  We need to check
	 * that we allow it in.
	 *
	 * They will look like:
	 *	SECURITY USER [username]
	 */

	/* there must be some security info */
	if (security == NULL) {
	    security_seterror(&rh->sech,
		"no bsd SECURITY for P_REQ");
	    amfree(service);
	    return (-1);
	}

	/* second word must be USER */
	if ((tok = strtok(security, " ")) == NULL) {
	    security_seterror(&rh->sech,
		"SECURITY line: %s", security_line);
	    amfree(service);
	    amfree(security_line);
	    return (-1);	/* default errmsg */
	}
	if (strcmp(tok, "USER") != 0) {
	    security_seterror(&rh->sech,
		"REQ SECURITY line parse error, expecting USER, got %s", tok);
	    amfree(service);
	    amfree(security_line);
	    return (-1);
	}

	/* the third word is the username */
	if ((tok = strtok(NULL, "")) == NULL) {
	    security_seterror(&rh->sech,
		"SECURITY line: %s", security_line);
	    amfree(security_line);
	    return (-1);	/* default errmsg */
	}
	if ((result = check_user(rh, tok, service)) != NULL) {
	    security_seterror(&rh->sech, "%s", result);
	    amfree(service);
	    amfree(result);
	    amfree(security_line);
	    return (-1);
	}

	/* we're good to go */
	break;
    default:
	break;
    }
    amfree(service);
    amfree(security_line);

    /*
     * If there is security info at the front of the packet, we need to
     * shift the rest of the data up and nuke it.
     */
    if (body != pkt->body)
	memmove(pkt->body, body, strlen(body) + 1);
    return (0);
}

/*
 * Transmit a packet.  Add security information first.
 */
ssize_t
udpbsd_sendpkt(
    void *	cookie,
    pkt_t *	pkt)
{
    struct sec_handle *rh = cookie;
    struct passwd *pwd;

    assert(rh != NULL);
    assert(pkt != NULL);

    auth_debug(1, ("%s: udpbsd_sendpkt: enter\n", get_pname()));
    /*
     * Initialize this datagram, and add the header
     */
    dgram_zero(&rh->udp->dgram);
    dgram_cat(&rh->udp->dgram, pkthdr2str(rh, pkt));

    /*
     * Add the security info.  This depends on which kind of packet we're
     * sending.
     */
    switch (pkt->type) {
    case P_REQ:
	/*
	 * Requests get sent with our username in the body
	 */
	if ((pwd = getpwuid(geteuid())) == NULL) {
	    security_seterror(&rh->sech,
		"can't get login name for my uid %ld", (long)getuid());
	    return (-1);
	}
	dgram_cat(&rh->udp->dgram, "SECURITY USER %s\n", pwd->pw_name);
	break;

    default:
	break;
    }

    /*
     * Add the body, and send it
     */
    dgram_cat(&rh->udp->dgram, pkt->body);

    auth_debug(1,
     ("%s: sec: udpbsd_sendpkt: %s (%d) pkt_t (len " SIZE_T_FMT ") contains:\n\n\"%s\"\n\n",
      debug_prefix_time(NULL), pkt_type2str(pkt->type), pkt->type,
      strlen(pkt->body), pkt->body));

    if (dgram_send_addr(&rh->peer, &rh->udp->dgram) != 0) {
	security_seterror(&rh->sech,
	    "send %s to %s failed: %s", pkt_type2str(pkt->type),
	    rh->hostname, strerror(errno));
	return (-1);
    }
    return (0);
}

void
udp_close(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    if (rh->proto_handle == NULL) {
	return;
    }

    auth_debug(1, ("%s: udp: close handle '%s'\n",
		   debug_prefix_time(NULL), rh->proto_handle));

    udp_recvpkt_cancel(rh);
    if (rh->next) {
	rh->next->prev = rh->prev;
    }
    else {
	rh->udp->bh_last = rh->prev;
    }
    if (rh->prev) {
	rh->prev->next = rh->next;
    }
    else {
	rh->udp->bh_first = rh->next;
    }

    amfree(rh->proto_handle);
    amfree(rh->hostname);
    amfree(rh);
}

/*
 * Set up to receive a packet asynchronously, and call back when it has
 * been read.
 */
void
udp_recvpkt(
    void *	cookie,
    void	(*fn)(void *, pkt_t *, security_status_t),
    void *	arg,
    int		timeout)
{
    struct sec_handle *rh = cookie;

    auth_debug(1, ("%s: udp_recvpkt(cookie=%p, fn=%p, arg=%p, timeout=%u)\n",
		   debug_prefix_time(NULL), cookie, fn, arg, timeout));
    assert(rh != NULL);
    assert(fn != NULL);


    /*
     * Subsequent recvpkt calls override previous ones
     */
    if (rh->ev_read == NULL) {
	udp_addref(rh->udp, &udp_netfd_read_callback);
	rh->ev_read = event_register(rh->event_id, EV_WAIT,
	    udp_recvpkt_callback, rh);
    }
    if (rh->ev_timeout != NULL)
	event_release(rh->ev_timeout);
    if (timeout < 0)
	rh->ev_timeout = NULL;
    else
	rh->ev_timeout = event_register((event_id_t)timeout, EV_TIME,
					udp_recvpkt_timeout, rh);
    rh->fn.recvpkt = fn;
    rh->arg = arg;
}

/*
 * Remove a async receive request on this handle from the queue.
 * If it is the last one to be removed, then remove the event
 * handler for our network fd
 */
void
udp_recvpkt_cancel(
    void *	cookie)
{
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    if (rh->ev_read != NULL) {
	udp_delref(rh->udp);
	event_release(rh->ev_read);
	rh->ev_read = NULL;
    }

    if (rh->ev_timeout != NULL) {
	event_release(rh->ev_timeout);
	rh->ev_timeout = NULL;
    }
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
void
udp_recvpkt_callback(
    void *	cookie)
{
    struct sec_handle *rh = cookie;
    void (*fn)(void *, pkt_t *, security_status_t);
    void *arg;

    auth_debug(1, ("%s: udp: receive handle '%s' netfd '%s'\n",
		   debug_prefix_time(NULL), rh->proto_handle, rh->udp->handle));
    assert(rh != NULL);

    /* if it doesn't correspond to this handle, something is wrong */
    assert(strcmp(rh->proto_handle, rh->udp->handle) == 0);

    /* if it didn't come from the same host/port, forget it */
    if (cmp_sockaddr(&rh->peer, &rh->udp->peer, 0) != 0) {
	amfree(rh->udp->handle);
	dbprintf(("not form same host\n"));
	dump_sockaddr(&rh->peer);
	dump_sockaddr(&rh->udp->peer);
	return;
    }

    /*
     * We need to cancel the recvpkt request before calling the callback
     * because the callback may reschedule us.
     */
    fn = rh->fn.recvpkt;
    arg = rh->arg;
    udp_recvpkt_cancel(rh);

    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the packet handling function instead of a packet.
     */
    if (rh->udp->recv_security_ok &&
	rh->udp->recv_security_ok(rh, &rh->udp->pkt) < 0) {
	(*fn)(arg, NULL, S_ERROR);
    } else {
	(*fn)(arg, &rh->udp->pkt, S_OK);
    }
}

/*
 * This is called when a handle times out before receiving a packet.
 */
void
udp_recvpkt_timeout(
    void *	cookie)
{
    struct sec_handle *rh = cookie;
    void (*fn)(void *, pkt_t *, security_status_t);
    void *arg;

    assert(rh != NULL);

    assert(rh->ev_timeout != NULL);
    fn = rh->fn.recvpkt;
    arg = rh->arg;
    udp_recvpkt_cancel(rh);
    (*fn)(arg, NULL, S_TIMEOUT);
}

/*
 * Given a hostname and a port, setup a udp_handle
 */
int
udp_inithandle(
    udp_handle_t *	udp,
    struct sec_handle *	rh,
    char *              hostname,
    struct sockaddr_storage *addr,
    in_port_t		port,
    char *		handle,
    int			sequence)
{
    /*
     * Save the hostname and port info
     */
    auth_debug(1, ("%s: udp_inithandle port %u handle %s sequence %d\n",
		   debug_prefix_time(NULL), (unsigned int)ntohs(port),
		   handle, sequence));
    assert(addr != NULL);

    rh->hostname = stralloc(hostname);
    memcpy(&rh->peer, addr, SIZEOF(rh->peer));
    SS_SET_PORT(&rh->peer, port);

    rh->prev = udp->bh_last;
    if (udp->bh_last) {
	rh->prev->next = rh;
    }
    if (!udp->bh_first) {
	udp->bh_first = rh;
    }
    rh->next = NULL;
    udp->bh_last = rh;

    rh->sequence = sequence;
    rh->event_id = (event_id_t)newevent++;
    amfree(rh->proto_handle);
    rh->proto_handle = stralloc(handle);
    rh->fn.connect = NULL;
    rh->arg = NULL;
    rh->ev_read = NULL;
    rh->ev_timeout = NULL;

    auth_debug(1, ("%s: udp: adding handle '%s'\n",
		   debug_prefix_time(NULL), rh->proto_handle));

    return(0);
}


/*
 * Callback for received packets.  This is the function bsd_recvpkt
 * registers with the event handler.  It is called when the event handler
 * realizes that data is waiting to be read on the network socket.
 */
void
udp_netfd_read_callback(
    void *	cookie)
{
    struct udp_handle *udp = cookie;
    struct sec_handle *rh;
    int a;
    char hostname[NI_MAXHOST];
    in_port_t port;
    char *errmsg = NULL;
    int result;

    auth_debug(1, ("%s: udp_netfd_read_callback(cookie=%p)\n",
		   debug_prefix_time(NULL), cookie));
    assert(udp != NULL);
    
#ifndef TEST							/* { */
    /*
     * Receive the packet.
     */
    dgram_zero(&udp->dgram);
    if (dgram_recv(&udp->dgram, 0, &udp->peer) < 0)
	return;
#endif /* !TEST */						/* } */

    /*
     * Parse the packet.
     */
    if (str2pkthdr(udp) < 0)
	return;

    /*
     * If there are events waiting on this handle, we're done
     */
    rh = udp->bh_first;
    while(rh != NULL && (strcmp(rh->proto_handle, udp->handle) != 0 ||
			 rh->sequence != udp->sequence ||
			 cmp_sockaddr(&rh->peer, &udp->peer, 0) != 0)) {
	rh = rh->next;
    }
    if (rh && event_wakeup(rh->event_id) > 0)
	return;

    /*
     * If we didn't find a handle, then check for a new incoming packet.
     * If no accept handler was setup, then just return.
     */
    if (udp->accept_fn == NULL)
	return;

    rh = alloc(SIZEOF(*rh));
    rh->proto_handle=NULL;
    rh->udp = udp;
    rh->rc = NULL;
    security_handleinit(&rh->sech, udp->driver);

    result = getnameinfo((struct sockaddr *)&udp->peer, SS_LEN(&udp->peer),
			 hostname, sizeof(hostname), NULL, 0, 0);
    if (result < 0) {
	dbprintf(("%s: getnameinfo failed: %s\n",
		  debug_prefix_time(NULL), gai_strerror(result)));
	security_seterror(&rh->sech, "getnameinfo failed: %s",
			  gai_strerror(result));
	return;
    }
    if (check_name_give_sockaddr(hostname,
				 (struct sockaddr *)&udp->peer, &errmsg) < 0) {
	security_seterror(&rh->sech, "%s",errmsg);
	amfree(errmsg);
	amfree(rh);
	return;
    }

    port = SS_GET_PORT(&udp->peer);
    a = udp_inithandle(udp, rh,
		   hostname,
		   &udp->peer,
		   port,
		   udp->handle,
		   udp->sequence);
    if (a < 0) {
	auth_debug(1, ("%s: bsd: closeX handle '%s'\n",
		       debug_prefix_time(NULL), rh->proto_handle));

	amfree(rh);
	return;
    }
    /*
     * Check the security of the packet.  If it is bad, then pass NULL
     * to the accept function instead of a packet.
     */
    if (rh->udp->recv_security_ok(rh, &udp->pkt) < 0)
	(*udp->accept_fn)(&rh->sech, NULL);
    else
	(*udp->accept_fn)(&rh->sech, &udp->pkt);
}

/*
 * Locate an existing connection to the given host, or create a new,
 * unconnected entry if none exists.  The caller is expected to check
 * for the lack of a connection (rc->read == -1) and set one up.
 */
struct tcp_conn *
sec_tcp_conn_get(
    const char *hostname,
    int		want_new)
{
    struct tcp_conn *rc;

    auth_debug(1, ("%s: sec_tcp_conn_get: %s\n",
		   debug_prefix_time(NULL), hostname));

    if (want_new == 0) {
	for (rc = connq_first(); rc != NULL; rc = connq_next(rc)) {
	    if (strcasecmp(hostname, rc->hostname) == 0)
		break;
	}

	if (rc != NULL) {
	    rc->refcnt++;
	    auth_debug(1,
		      ("%s: sec_tcp_conn_get: exists, refcnt to %s is now %d\n",
		       debug_prefix_time(NULL),
		       rc->hostname, rc->refcnt));
	    return (rc);
	}
    }

    auth_debug(1, ("%s: sec_tcp_conn_get: creating new handle\n",
		   debug_prefix_time(NULL)));
    /*
     * We can't be creating a new handle if we are the client
     */
    rc = alloc(SIZEOF(*rc));
    rc->read = rc->write = -1;
    rc->driver = NULL;
    rc->pid = -1;
    rc->ev_read = NULL;
    rc->toclose = 0;
    rc->donotclose = 0;
    strncpy(rc->hostname, hostname, SIZEOF(rc->hostname) - 1);
    rc->hostname[SIZEOF(rc->hostname) - 1] = '\0';
    rc->errmsg = NULL;
    rc->refcnt = 1;
    rc->handle = -1;
    rc->pkt = NULL;
    rc->accept_fn = NULL;
    rc->recv_security_ok = NULL;
    rc->prefix_packet = NULL;
    rc->auth = 0;
    connq_append(rc);
    return (rc);
}

/*
 * Delete a reference to a connection, and close it if it is the last
 * reference.
 */
void
sec_tcp_conn_put(
    struct tcp_conn *	rc)
{
    amwait_t status;

    assert(rc->refcnt > 0);
    --rc->refcnt;
    auth_debug(1, ("%s: sec_tcp_conn_put: decrementing refcnt for %s to %d\n",
		   debug_prefix_time(NULL),
	rc->hostname, rc->refcnt));
    if (rc->refcnt > 0) {
	return;
    }
    auth_debug(1, ("%s: sec_tcp_conn_put: closing connection to %s\n",
		   debug_prefix_time(NULL), rc->hostname));
    if (rc->read != -1)
	aclose(rc->read);
    if (rc->write != -1)
	aclose(rc->write);
    if (rc->pid != -1) {
	waitpid(rc->pid, &status, WNOHANG);
    }
    if (rc->ev_read != NULL)
	event_release(rc->ev_read);
    if (rc->errmsg != NULL)
	amfree(rc->errmsg);
    connq_remove(rc);
    amfree(rc->pkt);
    if(!rc->donotclose)
	amfree(rc); /* someone might still use it           */
		    /* eg. in sec_tcp_conn_read_callback if */
		    /*     event_wakeup call us.            */
}

/*
 * Turn on read events for a conn.  Or, increase a ev_read_refcnt if we are
 * already receiving read events.
 */
void
sec_tcp_conn_read(
    struct tcp_conn *	rc)
{
    assert (rc != NULL);

    if (rc->ev_read != NULL) {
	rc->ev_read_refcnt++;
	auth_debug(1,
	      ("%s: sec: conn_read: incremented ev_read_refcnt to %d for %s\n",
	       debug_prefix_time(NULL), rc->ev_read_refcnt, rc->hostname));
	return;
    }
    auth_debug(1, ("%s: sec: conn_read registering event handler for %s\n",
		   debug_prefix_time(NULL), rc->hostname));
    rc->ev_read = event_register((event_id_t)rc->read, EV_READFD,
		sec_tcp_conn_read_callback, rc);
    rc->ev_read_refcnt = 1;
}

static void
sec_tcp_conn_read_cancel(
    struct tcp_conn *	rc)
{

    --rc->ev_read_refcnt;
    auth_debug(1,
       ("%s: sec: conn_read_cancel: decremented ev_read_refcnt to %d for %s\n",
	debug_prefix_time(NULL),
	rc->ev_read_refcnt, rc->hostname));
    if (rc->ev_read_refcnt > 0) {
	return;
    }
    auth_debug(1,
                ("%s: sec: conn_read_cancel: releasing event handler for %s\n",
	         debug_prefix_time(NULL), rc->hostname));
    event_release(rc->ev_read);
    rc->ev_read = NULL;
}

/*
 * This is called when a handle is woken up because data read off of the
 * net is for it.
 */
static void
recvpkt_callback(
    void *	cookie,
    void *	buf,
    ssize_t	bufsize)
{
    pkt_t pkt;
    struct sec_handle *rh = cookie;

    assert(rh != NULL);

    auth_debug(1, ("%s: sec: recvpkt_callback: " SSIZE_T_FMT "\n",
		   debug_prefix_time(NULL), bufsize));
    /*
     * We need to cancel the recvpkt request before calling
     * the callback because the callback may reschedule us.
     */
    stream_recvpkt_cancel(rh);

    switch (bufsize) {
    case 0:
	security_seterror(&rh->sech,
	    "EOF on read from %s", rh->hostname);
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    case -1:
	security_seterror(&rh->sech, security_stream_geterror(&rh->rs->secstr));
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
	return;
    default:
	break;
    }

    parse_pkt(&pkt, buf, (size_t)bufsize);
    auth_debug(1,
	  ("%s: sec: received %s packet (%d) from %s, contains:\n\n\"%s\"\n\n",
	   debug_prefix_time(NULL), pkt_type2str(pkt.type), pkt.type,
	   rh->hostname, pkt.body));
    if (rh->rc->recv_security_ok && (rh->rc->recv_security_ok)(rh, &pkt) < 0)
	(*rh->fn.recvpkt)(rh->arg, NULL, S_ERROR);
    else
	(*rh->fn.recvpkt)(rh->arg, &pkt, S_OK);
    amfree(pkt.body);
}

/*
 * Callback for tcpm_stream_read_sync
 */
static void
stream_read_sync_callback(
    void *	s)
{
    struct sec_stream *rs = s;
    assert(rs != NULL);

    auth_debug(1, ("%s: sec: stream_read_callback_sync: handle %d\n",
		   debug_prefix_time(NULL), rs->handle));

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
        auth_debug(1, ("%s: sec: stream_read_callback_sync: it was for us\n",
		       debug_prefix_time(NULL)));
        rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
        auth_debug(1, ("%s: sec: stream_read_callback_sync: not for us\n",
		       debug_prefix_time(NULL)));
        return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */
    tcpm_stream_read_cancel(rs);

    if (rs->rc->pktlen <= 0) {
	auth_debug(1, ("%s: sec: stream_read_sync_callback: %s\n",
		       debug_prefix_time(NULL), rs->rc->errmsg));
	security_stream_seterror(&rs->secstr, rs->rc->errmsg);
	if(rs->closed_by_me == 0 && rs->closed_by_network == 0)
	    sec_tcp_conn_put(rs->rc);
	rs->closed_by_network = 1;
	return;
    }
    auth_debug(1,
	    ("%s: sec: stream_read_callback_sync: read " SSIZE_T_FMT " bytes from %s:%d\n",
	     debug_prefix_time(NULL),
        rs->rc->pktlen, rs->rc->hostname, rs->handle));
}

/*
 * Callback for tcpm_stream_read
 */
static void
stream_read_callback(
    void *	arg)
{
    struct sec_stream *rs = arg;
    assert(rs != NULL);

    auth_debug(1, ("%s: sec: stream_read_callback: handle %d\n",
		   debug_prefix_time(NULL), rs->handle));

    /*
     * Make sure this was for us.  If it was, then blow away the handle
     * so it doesn't get claimed twice.  Otherwise, leave it alone.
     *
     * If the handle is EOF, pass that up to our callback.
     */
    if (rs->rc->handle == rs->handle) {
	auth_debug(1, ("%s: sec: stream_read_callback: it was for us\n",
		       debug_prefix_time(NULL)));
	rs->rc->handle = H_TAKEN;
    } else if (rs->rc->handle != H_EOF) {
	auth_debug(1, ("%s: sec: stream_read_callback: not for us\n",
		       debug_prefix_time(NULL)));
	return;
    }

    /*
     * Remove the event first, and then call the callback.
     * We remove it first because we don't want to get in their
     * way if they reschedule it.
     */
    tcpm_stream_read_cancel(rs);

    if (rs->rc->pktlen <= 0) {
	auth_debug(1, ("%s: sec: stream_read_callback: %s\n",
		       debug_prefix_time(NULL), rs->rc->errmsg));
	security_stream_seterror(&rs->secstr, rs->rc->errmsg);
	if(rs->closed_by_me == 0 && rs->closed_by_network == 0)
	    sec_tcp_conn_put(rs->rc);
	rs->closed_by_network = 1;
	(*rs->fn)(rs->arg, NULL, rs->rc->pktlen);
	return;
    }
    auth_debug(1, ("%s: sec: stream_read_callback: read " SSIZE_T_FMT " bytes from %s:%d\n",
		   debug_prefix_time(NULL),
	rs->rc->pktlen, rs->rc->hostname, rs->handle));
    (*rs->fn)(rs->arg, rs->rc->pkt, rs->rc->pktlen);
    auth_debug(1, ("%s: sec: after callback stream_read_callback\n",
		   debug_prefix_time(NULL)));
}

/*
 * The callback for the netfd for the event handler
 * Determines if this packet is for this security handle,
 * and does the real callback if so.
 */
static void
sec_tcp_conn_read_callback(
    void *	cookie)
{
    struct tcp_conn *	rc = cookie;
    struct sec_handle *	rh;
    pkt_t		pkt;
    ssize_t		rval;
    int			revent;

    assert(cookie != NULL);

    auth_debug(1, ("%s: sec: conn_read_callback\n", debug_prefix_time(NULL)));

    /* Read the data off the wire.  If we get errors, shut down. */
    rval = tcpm_recv_token(rc, rc->read, &rc->handle, &rc->errmsg, &rc->pkt,
				&rc->pktlen, 60);
    auth_debug(1, ("%s: sec: conn_read_callback: tcpm_recv_token returned " SSIZE_T_FMT "\n",
		   debug_prefix_time(NULL), rval));
    if (rval < 0 || rc->handle == H_EOF) {
	rc->pktlen = rval;
	rc->handle = H_EOF;
	revent = event_wakeup((event_id_t)rc);
	auth_debug(1, ("%s: sec: conn_read_callback: event_wakeup return %d\n",
		       debug_prefix_time(NULL), revent));
	/* delete our 'accept' reference */
	if (rc->accept_fn != NULL) {
	    if(rc->refcnt != 1) {
		dbprintf(("STRANGE, rc->refcnt should be 1, it is %d\n",
			  rc->refcnt));
		rc->refcnt=1;
	    }
	    rc->accept_fn = NULL;
	    sec_tcp_conn_put(rc);
	}
	return;
    }

    if(rval == 0) {
	rc->pktlen = 0;
	revent = event_wakeup((event_id_t)rc);
	auth_debug(1,
		   ("%s: 0 sec: conn_read_callback: event_wakeup return %d\n",
		    debug_prefix_time(NULL), revent));
	return;
    }

    /* If there are events waiting on this handle, we're done */
    rc->donotclose = 1;
    revent = event_wakeup((event_id_t)rc);
    auth_debug(1, ("%s: sec: conn_read_callback: event_wakeup return " SSIZE_T_FMT "\n",
		   debug_prefix_time(NULL), rval));
    rc->donotclose = 0;
    if (rc->handle == H_TAKEN || rc->pktlen == 0) {
	if(rc->refcnt == 0) amfree(rc);
	return;
    }

    assert(rc->refcnt > 0);

    /* If there is no accept fn registered, then drop the packet */
    if (rc->accept_fn == NULL)
	return;

    rh = alloc(SIZEOF(*rh));
    security_handleinit(&rh->sech, rc->driver);
    rh->hostname = stralloc(rc->hostname);
    rh->ev_timeout = NULL;
    rh->rc = rc;
    rh->peer = rc->peer;
    rh->rs = tcpma_stream_client(rh, rc->handle);

    auth_debug(1, ("%s: sec: new connection\n", debug_prefix_time(NULL)));
    pkt.body = NULL;
    parse_pkt(&pkt, rc->pkt, (size_t)rc->pktlen);
    auth_debug(1, ("%s: sec: calling accept_fn\n", debug_prefix_time(NULL)));
    if (rh->rc->recv_security_ok && (rh->rc->recv_security_ok)(rh, &pkt) < 0)
	(*rc->accept_fn)(&rh->sech, NULL);
    else
	(*rc->accept_fn)(&rh->sech, &pkt);
    amfree(pkt.body);
}

void
parse_pkt(
    pkt_t *	pkt,
    const void *buf,
    size_t	bufsize)
{
    const unsigned char *bufp = buf;

    auth_debug(1, ("%s: sec: parse_pkt: parsing buffer of " SSIZE_T_FMT " bytes\n",
		   debug_prefix_time(NULL), bufsize));

    pkt->type = (pktype_t)*bufp++;
    bufsize--;

    pkt->packet_size = bufsize+1;
    pkt->body = alloc(pkt->packet_size);
    if (bufsize == 0) {
	pkt->body[0] = '\0';
    } else {
	memcpy(pkt->body, bufp, bufsize);
	pkt->body[pkt->packet_size - 1] = '\0';
    }
    pkt->size = strlen(pkt->body);

    auth_debug(1, ("%s: sec: parse_pkt: %s (%d): \"%s\"\n",
		   debug_prefix_time(NULL), pkt_type2str(pkt->type),
		   pkt->type, pkt->body));
}

/*
 * Convert a packet header into a string
 */
const char *
pkthdr2str(
    const struct sec_handle *	rh,
    const pkt_t *		pkt)
{
    static char retbuf[256];

    assert(rh != NULL);
    assert(pkt != NULL);

    snprintf(retbuf, SIZEOF(retbuf), "Amanda %d.%d %s HANDLE %s SEQ %d\n",
	VERSION_MAJOR, VERSION_MINOR, pkt_type2str(pkt->type),
	rh->proto_handle, rh->sequence);

    auth_debug(1, ("%s: bsd: pkthdr2str handle '%s'\n",
		   debug_prefix_time(NULL), rh->proto_handle));

    /* check for truncation.  If only we had asprintf()... */
    assert(retbuf[strlen(retbuf) - 1] == '\n');

    return (retbuf);
}

/*
 * Parses out the header line in 'str' into the pkt and handle
 * Returns negative on parse error.
 */
int
str2pkthdr(
    udp_handle_t *	udp)
{
    char *str;
    const char *tok;
    pkt_t *pkt;

    pkt = &udp->pkt;

    assert(udp->dgram.cur != NULL);
    str = stralloc(udp->dgram.cur);

    /* "Amanda %d.%d <ACK,NAK,...> HANDLE %s SEQ %d\n" */

    /* Read in "Amanda" */
    if ((tok = strtok(str, " ")) == NULL || strcmp(tok, "Amanda") != 0)
	goto parse_error;

    /* nothing is done with the major/minor numbers currently */
    if ((tok = strtok(NULL, " ")) == NULL || strchr(tok, '.') == NULL)
	goto parse_error;

    /* Read in the packet type */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    amfree(pkt->body);
    pkt_init_empty(pkt, pkt_str2type(tok));
    if (pkt->type == (pktype_t)-1)    
	goto parse_error;

    /* Read in "HANDLE" */
    if ((tok = strtok(NULL, " ")) == NULL || strcmp(tok, "HANDLE") != 0)
	goto parse_error;

    /* parse the handle */
    if ((tok = strtok(NULL, " ")) == NULL)
	goto parse_error;
    amfree(udp->handle);
    udp->handle = stralloc(tok);

    /* Read in "SEQ" */
    if ((tok = strtok(NULL, " ")) == NULL || strcmp(tok, "SEQ") != 0)   
	goto parse_error;

    /* parse the sequence number */   
    if ((tok = strtok(NULL, "\n")) == NULL)
	goto parse_error;
    udp->sequence = atoi(tok);

    /* Save the body, if any */       
    if ((tok = strtok(NULL, "")) != NULL)
	pkt_cat(pkt, "%s", tok);

    amfree(str);
    return (0);

parse_error:
#if 0 /* XXX we have no way of passing this back up */
    security_seterror(&rh->sech,
	"parse error in packet header : '%s'", origstr);
#endif
    amfree(str);
    return (-1);
}

char *
check_user(
    struct sec_handle *	rh,
    const char *	remoteuser,
    const char *	service)
{
    struct passwd *pwd;
    char *r;
    char *result = NULL;
    char *localuser = NULL;

    /* lookup our local user name */
    if ((pwd = getpwnam(CLIENT_LOGIN)) == NULL) {
	return vstralloc("getpwnam(", CLIENT_LOGIN, ") fails", NULL);
    }

    /*
     * Make a copy of the user name in case getpw* is called by
     * any of the lower level routines.
     */
    localuser = stralloc(pwd->pw_name);

#ifndef USE_AMANDAHOSTS
    r = check_user_ruserok(rh->hostname, pwd, remoteuser);
#else
    r = check_user_amandahosts(rh->hostname, &rh->peer, pwd, remoteuser, service);
#endif
    if (r != NULL) {
	result = vstralloc("user ", remoteuser, " from ", rh->hostname,
			   " is not allowed to execute the service ",
			   service, ": ", r, NULL);
	amfree(r);
    }
    amfree(localuser);
    return result;
}

/*
 * See if a remote user is allowed in.  This version uses ruserok()
 * and friends.
 *
 * Returns 0 on success, or negative on error.
 */
char *
check_user_ruserok(
    const char *	host,
    struct passwd *	pwd,
    const char *	remoteuser)
{
    int saved_stderr;
    int fd[2];
    FILE *fError;
    amwait_t exitcode;
    pid_t ruserok_pid;
    pid_t pid;
    char *es;
    char *result;
    int ok;
    char number[NUM_STR_SIZE];
    uid_t myuid = getuid();

    /*
     * note that some versions of ruserok (eg SunOS 3.2) look in
     * "./.rhosts" rather than "~CLIENT_LOGIN/.rhosts", so we have to
     * chdir ourselves.  Sigh.
     *
     * And, believe it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    if (pipe(fd) != 0) {
	return stralloc2("pipe() fails: ", strerror(errno));
    }
    if ((ruserok_pid = fork()) < 0) {
	return stralloc2("fork() fails: ", strerror(errno));
    } else if (ruserok_pid == 0) {
	int ec;

	close(fd[0]);
	fError = fdopen(fd[1], "w");
	if (!fError) {
	    error("Can't fdopen: %s", strerror(errno));
	    /*NOTREACHED*/
	}
	/* pamper braindead ruserok's */
	if (chdir(pwd->pw_dir) != 0) {
	    fprintf(fError, "chdir(%s) failed: %s",
		    pwd->pw_dir, strerror(errno));
	    fclose(fError);
	    exit(1);
	}

	if (debug_auth >= 9) {
	    char *dir = stralloc(pwd->pw_dir);

	    auth_debug(9, ("%s: bsd: calling ruserok(%s, %d, %s, %s)\n",
			   debug_prefix_time(NULL), host,
			   ((myuid == 0) ? 1 : 0), remoteuser, pwd->pw_name));
	    if (myuid == 0) {
		auth_debug(9, ("%s: bsd: because you are running as root, ",
			       debug_prefix_time(NULL)));
		auth_debug(9, ("/etc/hosts.equiv will not be used\n"));
	    } else {
		show_stat_info("/etc/hosts.equiv", NULL);
	    }
	    show_stat_info(dir, "/.rhosts");
	    amfree(dir);
	}

	saved_stderr = dup(2);
	close(2);
	if (open("/dev/null", O_RDWR) == -1) {
            auth_debug(1, ("%s: Could not open /dev/null: %s\n",
			   debug_prefix_time(NULL), strerror(errno)));
	    ec = 1;
	} else {
	    ok = ruserok(host, myuid == 0, remoteuser, CLIENT_LOGIN);
	    if (ok < 0) {
	        ec = 1;
	    } else {
	        ec = 0;
	    }
	}
	(void)dup2(saved_stderr,2);
	close(saved_stderr);
	exit(ec);
    }
    close(fd[1]);
    fError = fdopen(fd[0], "r");
    if (!fError) {
	error("Can't fdopen: %s", strerror(errno));
	/*NOTREACHED*/
    }

    result = NULL;
    while ((es = agets(fError)) != NULL) {
	if (*es == 0) {
	    amfree(es);
	    continue;
	}
	if (result == NULL) {
	    result = stralloc("");
	} else {
	    strappend(result, ": ");
	}
	strappend(result, es);
	amfree(es);
    }
    close(fd[0]);

    pid = wait(&exitcode);
    while (pid != ruserok_pid) {
	if ((pid == (pid_t) -1) && (errno != EINTR)) {
	    amfree(result);
	    return stralloc2("ruserok wait failed: %s", strerror(errno));
	}
	pid = wait(&exitcode);
    }
    if (WIFSIGNALED(exitcode)) {
	amfree(result);
	snprintf(number, SIZEOF(number), "%d", WTERMSIG(exitcode));
	return stralloc2("ruserok child got signal ", number);
    }
    if (WEXITSTATUS(exitcode) == 0) {
	amfree(result);
    } else if (result == NULL) {
	result = stralloc("ruserok failed");
    }

    return result;
}

/*
 * Check to see if a user is allowed in.  This version uses .amandahosts
 * Returns -1 on failure, or 0 on success.
 */
char *
check_user_amandahosts(
    const char *	host,
    struct sockaddr_storage *addr,
    struct passwd *	pwd,
    const char *	remoteuser,
    const char *	service)
{
    char *line = NULL;
    char *filehost;
    const char *fileuser;
    char *ptmp = NULL;
    char *result = NULL;
    FILE *fp = NULL;
    int found;
    struct stat sbuf;
    char n1[NUM_STR_SIZE];
    char n2[NUM_STR_SIZE];
    int hostmatch;
    int usermatch;
    char *aservice = NULL;
#ifdef WORKING_IPV6
    char ipstr[INET6_ADDRSTRLEN];
#else
    char ipstr[INET_ADDRSTRLEN];
#endif

    auth_debug(1, ("check_user_amandahosts(host=%s, pwd=%p, "
		   "remoteuser=%s, service=%s)\n",
		   host, pwd, remoteuser, service));

    ptmp = stralloc2(pwd->pw_dir, "/.amandahosts");
    if (debug_auth >= 9) {
	show_stat_info(ptmp, "");;
    }
    if ((fp = fopen(ptmp, "r")) == NULL) {
	result = vstralloc("cannot open ", ptmp, ": ", strerror(errno), NULL);
	amfree(ptmp);
	return result;
    }

    /*
     * Make sure the file is owned by the Amanda user and does not
     * have any group/other access allowed.
     */
    if (fstat(fileno(fp), &sbuf) != 0) {
	result = vstralloc("cannot fstat ", ptmp, ": ", strerror(errno), NULL);
	goto common_exit;
    }
    if (sbuf.st_uid != pwd->pw_uid) {
	snprintf(n1, SIZEOF(n1), "%ld", (long)sbuf.st_uid);
	snprintf(n2, SIZEOF(n2), "%ld", (long)pwd->pw_uid);
	result = vstralloc(ptmp, ": ",
			   "owned by id ", n1,
			   ", should be ", n2,
			   NULL);
	goto common_exit;
    }
    if ((sbuf.st_mode & 077) != 0) {
	result = stralloc2(ptmp,
	  ": incorrect permissions; file must be accessible only by its owner");
	goto common_exit;
    }

    /*
     * Now, scan the file for the host/user/service.
     */
    found = 0;
    while ((line = agets(fp)) != NULL) {
	if (*line == 0) {
	    amfree(line);
	    continue;
	}

	auth_debug(9, ("%s: bsd: processing line: <%s>\n",
		       debug_prefix_time(NULL), line));
	/* get the host out of the file */
	if ((filehost = strtok(line, " \t")) == NULL) {
	    amfree(line);
	    continue;
	}

	/* get the username.  If no user specified, then use the local user */
	if ((fileuser = strtok(NULL, " \t")) == NULL) {
	    fileuser = pwd->pw_name;
	}

	hostmatch = (strcasecmp(filehost, host) == 0);
	/*  ok if addr=127.0.0.1 and
	 *  either localhost or localhost.domain is in .amandahost */
	if (!hostmatch  &&
	    (strcasecmp(filehost, "localhost")== 0 ||
	     strcasecmp(filehost, "localhost.localdomain")== 0)) {
#ifdef WORKING_IPV6
	    if (addr->ss_family == (sa_family_t)AF_INET6)
		inet_ntop(AF_INET6, &((struct sockaddr_in6 *)addr)->sin6_addr,
			  ipstr, sizeof(ipstr));
	    else
#endif
		inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr,
			  ipstr, sizeof(ipstr));
	    if (strcmp(ipstr, "127.0.0.1") == 0 ||
		strcmp(ipstr, "::1") == 0)
		hostmatch = 1;
	}
	usermatch = (strcasecmp(fileuser, remoteuser) == 0);
	auth_debug(9, ("%s: bsd: comparing \"%s\" with\n",
		       debug_prefix_time(NULL), filehost));
	auth_debug(9, ("%s: bsd:           \"%s\" (%s)\n", host,
		  debug_prefix_time(NULL), hostmatch ? "match" : "no match"));
	auth_debug(9, ("%s: bsd:       and \"%s\" with\n",
		       fileuser, debug_prefix_time(NULL)));
	auth_debug(9, ("%s: bsd:           \"%s\" (%s)\n", remoteuser,
		       debug_prefix_time(NULL), usermatch ? "match" : "no match"));
	/* compare */
	if (!hostmatch || !usermatch) {
	    amfree(line);
	    continue;
	}

        if (!service) {
	    /* success */
	    amfree(line);
	    found = 1;
	    break;
	}

	/* get the services.  If no service specified, then use
	 * noop/selfcheck/sendsize/sendbackup
         */
	aservice = strtok(NULL, " \t,");
	if (!aservice) {
	    if (strcmp(service,"noop") == 0 ||
	       strcmp(service,"selfcheck") == 0 ||
	       strcmp(service,"sendsize") == 0 ||
	       strcmp(service,"sendbackup") == 0) {
		/* success */
		found = 1;
		amfree(line);
		break;
	    }
	    else {
		amfree(line);
		break;
	    }
	}

	do {
	    if (strcmp(aservice,service) == 0) {
		found = 1;
		break;
	    }
	    if (strcmp(aservice, "amdump") == 0 && 
	       (strcmp(service, "noop") == 0 ||
		strcmp(service, "selfcheck") == 0 ||
		strcmp(service, "sendsize") == 0 ||
		strcmp(service, "sendbackup") == 0)) {
		found = 1;
		break;
	    }
	} while((aservice = strtok(NULL, " \t,")) != NULL);

	if (aservice && strcmp(aservice, service) == 0) {
	    /* success */
	    found = 1;
	    amfree(line);
	    break;
	}
	amfree(line);
    }
    if (! found) {
	if (strcmp(service, "amindexd") == 0 ||
	    strcmp(service, "amidxtaped") == 0) {
	    result = vstralloc("Please add \"amindexd amidxtaped\" to "
			       "the line in ", ptmp, " on the client", NULL);
	} else if (strcmp(service, "amdump") == 0 ||
		   strcmp(service, "noop") == 0 ||
		   strcmp(service, "selfcheck") == 0 ||
		   strcmp(service, "sendsize") == 0 ||
		   strcmp(service, "sendbackup") == 0) {
	    result = vstralloc("Please add \"amdump\" to the line in ",
			       ptmp, " on the client", NULL);
	} else {
	    result = vstralloc(ptmp, ": ",
			       "invalid service ", service, NULL);
	}
    }

common_exit:

    afclose(fp);
    amfree(ptmp);

    return result;
}

/* return 1 on success, 0 on failure */
int
check_security(
    struct sockaddr_storage *addr,
    char *		str,
    unsigned long	cksum,
    char **		errstr)
{
    char *		remotehost = NULL, *remoteuser = NULL;
    char *		bad_bsd = NULL;
    struct passwd *	pwptr;
    uid_t		myuid;
    char *		s;
    char *		fp;
    int			ch;
    char		hostname[NI_MAXHOST];
    in_port_t		port;
    int			result;

    (void)cksum;	/* Quiet unused parameter warning */

    auth_debug(1,
	       ("%s: check_security(addr=%p, str='%s', cksum=%lu, errstr=%p\n",
		debug_prefix_time(NULL), addr, str, cksum, errstr));
    dump_sockaddr(addr);

    *errstr = NULL;

    /* what host is making the request? */
    if ((result = getnameinfo((struct sockaddr *)addr, SS_LEN(addr),
                              hostname, NI_MAXHOST, NULL, 0, 0) == -1)) {
	dbprintf(("%s: getnameinfo failed: %s\n",
		  debug_prefix_time(NULL), gai_strerror(result)));
	*errstr = vstralloc("[", "addr ", str_sockaddr(addr), ": ",
			    "getnameinfo failed: ", gai_strerror(result),
			    "]", NULL);
	return 0;
    }
    remotehost = stralloc(hostname);
    if( check_name_give_sockaddr(hostname,
				 (struct sockaddr *)addr, errstr) < 0) {
	amfree(remotehost);
	return 0;
    }

    /* next, make sure the remote port is a "reserved" one */
    port = SS_GET_PORT(addr);
    if (port >= IPPORT_RESERVED) {
	char number[NUM_STR_SIZE];

	snprintf(number, SIZEOF(number), "%u", (unsigned int)port);
	*errstr = vstralloc("[",
			    "host ", remotehost, ": ",
			    "port ", number, " not secure",
			    "]", NULL);
	amfree(remotehost);
	return 0;
    }

    /* extract the remote user name from the message */

    s = str;
    ch = *s++;

    bad_bsd = vstralloc("[",
			"host ", remotehost, ": ",
			"bad bsd security line",
			"]", NULL);

    if (strncmp_const_skip(s - 1, "USER ", s, ch) != 0) {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }

    skip_whitespace(s, ch);
    if (ch == '\0') {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    remoteuser = stralloc(fp);
    s[-1] = (char)ch;
    amfree(bad_bsd);

    /* lookup our local user name */

    myuid = getuid();
    if ((pwptr = getpwuid(myuid)) == NULL)
        error("error [getpwuid(%d) fails]", myuid);

    auth_debug(1, ("%s: bsd security: remote host %s user %s local user %s\n",
		   debug_prefix_time(NULL), remotehost, remoteuser, pwptr->pw_name));

#ifndef USE_AMANDAHOSTS
    s = check_user_ruserok(remotehost, pwptr, remoteuser);
#else
    s = check_user_amandahosts(remotehost, addr, pwptr, remoteuser, NULL);
#endif

    if (s != NULL) {
	*errstr = vstralloc("[",
			    "access as ", pwptr->pw_name, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    ": ", s, "]", NULL);
    }
    amfree(s);
    amfree(remotehost);
    amfree(remoteuser);
    return *errstr == NULL;
}

/*
 * Writes out the entire iovec
 */
ssize_t
net_writev(
    int			fd,
    struct iovec *	iov,
    int			iovcnt)
{
    ssize_t delta, n, total;

    assert(iov != NULL);

    total = 0;
    while (iovcnt > 0) {
	/*
	 * Write the iovec
	 */
	n = writev(fd, iov, iovcnt);
	if (n < 0) {
	    if (errno != EINTR)
		return (-1);
	    auth_debug(1, ("%s: net_writev got EINTR\n",
			   debug_prefix_time(NULL)));
	}
	else if (n == 0) {
	    errno = EIO;
	    return (-1);
	} else {
	    total += n;
	    /*
	     * Iterate through each iov.  Figure out what we still need
	     * to write out.
	     */
	    for (; n > 0; iovcnt--, iov++) {
		/* 'delta' is the bytes written from this iovec */
		delta = ((size_t)n < iov->iov_len) ? n : (ssize_t)iov->iov_len;
		/* subtract from the total num bytes written */
		n -= delta;
		assert(n >= 0);
		/* subtract from this iovec */
		iov->iov_len -= delta;
		iov->iov_base = (char *)iov->iov_base + delta;
		/* if this iovec isn't empty, run the writev again */
		if (iov->iov_len > 0)
		    break;
	    }
	}
    }
    return (total);
}

/*
 * Like read(), but waits until the entire buffer has been filled.
 */
ssize_t
net_read(
    int		fd,
    void *	vbuf,
    size_t	origsize,
    int		timeout)
{
    char *buf = vbuf;	/* ptr arith */
    ssize_t nread;
    size_t size = origsize;

    auth_debug(1, ("%s: net_read: begin " SIZE_T_FMT "\n",
		   debug_prefix_time(NULL), origsize));

    while (size > 0) {
	auth_debug(1, ("%s: net_read: while " SIZE_T_FMT "\n",
		       debug_prefix_time(NULL), size));
	nread = net_read_fillbuf(fd, timeout, buf, size);
	if (nread < 0) {
    	    auth_debug(1, ("%s: db: net_read: end return(-1)\n",
			   debug_prefix_time(NULL)));
	    return (-1);
	}
	if (nread == 0) {
    	    auth_debug(1, ("%s: net_read: end return(0)\n",
			   debug_prefix_time(NULL)));
	    return (0);
	}
	buf += nread;
	size -= nread;
    }
    auth_debug(1, ("%s: net_read: end " SIZE_T_FMT "\n",
		   debug_prefix_time(NULL), origsize));
    return ((ssize_t)origsize);
}

/*
 * net_read likes to do a lot of little reads.  Buffer it.
 */
ssize_t
net_read_fillbuf(
    int		fd,
    int		timeout,
    void *	buf,
    size_t	size)
{
    SELECT_ARG_TYPE readfds;
    struct timeval tv;
    ssize_t nread;

    auth_debug(1, ("%s: net_read_fillbuf: begin\n", debug_prefix_time(NULL)));
    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    switch (select(fd + 1, &readfds, NULL, NULL, &tv)) {
    case 0:
	errno = ETIMEDOUT;
	/* FALLTHROUGH */
    case -1:
	auth_debug(1, ("%s: net_read_fillbuf: case -1\n",
		       debug_prefix_time(NULL)));
	return (-1);
    case 1:
	auth_debug(1, ("%s: net_read_fillbuf: case 1\n",
		       debug_prefix_time(NULL)));
	assert(FD_ISSET(fd, &readfds));
	break;
    default:
	auth_debug(1, ("%s: net_read_fillbuf: case default\n",
		       debug_prefix_time(NULL)));
	assert(0);
	break;
    }
    nread = read(fd, buf, size);
    if (nread < 0)
	return (-1);
    auth_debug(1, ("%s: net_read_fillbuf: end " SSIZE_T_FMT "\n",
		   debug_prefix_time(NULL), nread));
    return (nread);
}


/*
 * Display stat() information about a file.
 */

void
show_stat_info(
    char *	a,
    char *	b)
{
    char *name = vstralloc(a, b, NULL);
    struct stat sbuf;
    struct passwd *pwptr;
    char *owner;
    struct group *grptr;
    char *group;

    if (stat(name, &sbuf) != 0) {
	auth_debug(1, ("%s: bsd: cannot stat %s: %s\n",
		       debug_prefix_time(NULL), name, strerror(errno)));
	amfree(name);
	return;
    }
    if ((pwptr = getpwuid(sbuf.st_uid)) == NULL) {
	owner = alloc(NUM_STR_SIZE + 1);
	snprintf(owner, NUM_STR_SIZE, "%ld", (long)sbuf.st_uid);
    } else {
	owner = stralloc(pwptr->pw_name);
    }
    if ((grptr = getgrgid(sbuf.st_gid)) == NULL) {
	group = alloc(NUM_STR_SIZE + 1);
	snprintf(owner, NUM_STR_SIZE, "%ld", (long)sbuf.st_gid);
    } else {
	group = stralloc(grptr->gr_name);
    }
    auth_debug(1, ("%s: bsd: processing file: %s\n", debug_prefix_time(NULL), name));
    auth_debug(1, ("%s: bsd:                  owner=%s group=%s mode=%03o\n",
		   debug_prefix_time(NULL), owner, group,
		   (int) (sbuf.st_mode & 0777)));
    amfree(name);
    amfree(owner);
    amfree(group);
}

int
check_name_give_sockaddr(
    const char *hostname,
    struct sockaddr *addr,
    char **errstr)
{
    struct addrinfo *res = NULL, *res1;
    struct addrinfo hints;
    int result;

#ifdef WORKING_IPV6
    if ((addr)->sa_family == AF_INET6)
	hints.ai_flags = AI_CANONNAME | AI_V4MAPPED | AI_ALL;
    else
#endif
	hints.ai_flags = AI_CANONNAME;
    hints.ai_family = addr->sa_family;
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    result = getaddrinfo(hostname, NULL, &hints, &res);
    if (result != 0) {
	dbprintf(("check_name_give_sockaddr: getaddrinfo(%s): %s\n", hostname, gai_strerror(result)));
	*errstr = newvstralloc(*errstr,
			       " getaddrinfo(", hostname, "): ",
			       gai_strerror(result), NULL);
	return -1;
    }
    if (res->ai_canonname == NULL) {
	dbprintf(("getaddrinfo(%s) did not return a canonical name\n", hostname));
	*errstr = newvstralloc(*errstr, 
 		" getaddrinfo(", hostname, ") did not return a canonical name", NULL);
	return -1;
    }

    if (strncasecmp(hostname, res->ai_canonname, strlen(hostname)) != 0) {
	auth_debug(1, ("%s: %s doesn't resolve to itself, it resolves to %s\n",
		       debug_prefix_time(NULL),
		       hostname, res->ai_canonname));
	*errstr = newvstralloc(*errstr, hostname,
			       _(" doesn't resolve to itself, it resolves to "),
			       res->ai_canonname, NULL);
	return -1;
    }

    for(res1=res; res1 != NULL; res1 = res1->ai_next) {
	if (res1->ai_addr->sa_family == addr->sa_family) {
	    if (cmp_sockaddr((struct sockaddr_storage *)res1->ai_addr, (struct sockaddr_storage *)addr, 1) == 0) {
		freeaddrinfo(res);
		return 0;
	    }
	}
    }

    *errstr = newvstralloc(*errstr,
			   str_sockaddr((struct sockaddr_storage *)addr),
			   " doesn't resolve to ",
			   hostname, NULL);
    freeaddrinfo(res);
    return -1;
}


int
check_addrinfo_give_name(
    struct addrinfo *res,
    const char *hostname,
    char **errstr)
{
    if (strncasecmp(hostname, res->ai_canonname, strlen(hostname)) != 0) {
	dbprintf(("%s: %s doesn't resolve to itself, it resolv to %s\n",
		  debug_prefix_time(NULL),
		  hostname, res->ai_canonname));
	*errstr = newvstralloc(*errstr, hostname,
			       " doesn't resolve to itself, it resolv to ",
			       res->ai_canonname, NULL);
	return -1;
    }

    return 0;
}

/* Try resolving the hostname, just to catch any potential
 * problems down the road.  This is used from most security_connect
 * methods, many of which also want the canonical name.  Returns 
 * 0 on success.
 */
int
try_resolving_hostname(
	const char *hostname,
	char **canonname)
{
    struct addrinfo hints;
    struct addrinfo *gaires;
    int res;

#ifdef WORKING_IPV6
    hints.ai_flags = AI_CANONNAME | AI_V4MAPPED | AI_ALL;
    hints.ai_family = AF_UNSPEC;
#else
    hints.ai_flags = AI_CANONNAME;
    hints.ai_family = AF_INET;
#endif
    hints.ai_socktype = 0;
    hints.ai_protocol = 0;
    hints.ai_addrlen = 0;
    hints.ai_addr = NULL;
    hints.ai_canonname = NULL;
    hints.ai_next = NULL;
    if ((res = getaddrinfo(hostname, NULL, &hints, &gaires)) != 0) {
	return -1;
    }
    if (canonname && gaires && gaires->ai_canonname)
	*canonname = stralloc(gaires->ai_canonname);
    if (gaires) freeaddrinfo(gaires);

    return 0;
}
