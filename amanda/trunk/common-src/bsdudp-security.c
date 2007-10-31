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
 * $Id: bsdudp-security.c,v 1.7 2006/07/05 13:18:20 martinea Exp $
 *
 * "BSD" security module
 */

#include "amanda.h"
#include "util.h"
#include "clock.h"
#include "dgram.h"
#include "event.h"
#include "packet.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"
#include "version.h"

#ifndef SO_RCVBUF
#undef DUMPER_SOCKET_BUFFERING
#endif

/*
 * Interface functions
 */
static void bsdudp_connect(const char *,
    char *(*)(char *, void *), 
    void (*)(void *, security_handle_t *, security_status_t), void *, void *);
static void bsdudp_accept(const struct security_driver *,
    char *(*)(char *, void *),
    int, int,
    void (*)(security_handle_t *, pkt_t *),
    void *);
static void bsdudp_close(void *);

/*
 * This is our interface to the outside world
 */
const security_driver_t bsdudp_security_driver = {
    "BSDUDP",
    bsdudp_connect,
    bsdudp_accept,
    bsdudp_close,
    udpbsd_sendpkt,
    udp_recvpkt,
    udp_recvpkt_cancel,
    tcp1_stream_server,
    tcp1_stream_accept,
    tcp1_stream_client,
    tcpma_stream_close,
    sec_stream_auth,
    sec_stream_id,
    tcpm_stream_write,
    tcpm_stream_read,
    tcpm_stream_read_sync,
    tcpm_stream_read_cancel,
    sec_close_connection_none,
    NULL,
    NULL
};

/*
 * This is data local to the datagram socket.  We have one datagram
 * per process, so it is global.
 */
static udp_handle_t netfd4;
static udp_handle_t netfd6;
static int not_init4 = 1;
static int not_init6 = 1;

/* generate new handles from here */
static unsigned int newhandle = 0;

/*
 * Setup and return a handle outgoing to a client
 */
static void
bsdudp_connect(
    const char *hostname,
    char *	(*conf_fn)(char *, void *),
    void	(*fn)(void *, security_handle_t *, security_status_t),
    void *	arg,
    void *	datap)
{
    struct sec_handle *bh;
    struct servent *se;
    in_port_t port;
    struct timeval sequence_time;
    int sequence;
    char *handle;
    int result;
    char *canonname;
    struct addrinfo *res = NULL;

    (void)conf_fn;	/* Quiet unused parameter warning */
    (void)datap;	/* Quiet unused parameter warning */
    assert(hostname != NULL);

    bh = alloc(sizeof(*bh));
    bh->proto_handle=NULL;
    bh->rc = NULL;
    security_handleinit(&bh->sech, &bsdudp_security_driver);

    result = resolve_hostname(hostname, &res, &canonname);
    if(result != 0) {
	dbprintf(_("resolve_hostname(%s): %s\n"), hostname, gai_strerror(result));
	security_seterror(&bh->sech, _("resolve_hostname(%s): %s\n"), hostname,
			  gai_strerror(result));
	(*fn)(arg, &bh->sech, S_ERROR);
	return;
    }
    if (canonname == NULL) {
	dbprintf(_("resolve_hostname(%s) did not return a canonical name\n"), hostname);
	security_seterror(&bh->sech,
	        _("resolve_hostname(%s) did not return a canonical name\n"), hostname);
	(*fn)(arg, &bh->sech, S_ERROR);
       return;
    }
    if (res == NULL) {
	dbprintf(_("resolve_hostname(%s): no results\n"), hostname);
	security_seterror(&bh->sech,
	        _("resolve_hostname(%s): no results\n"), hostname);
	(*fn)(arg, &bh->sech, S_ERROR);
       amfree(canonname);
       return;
    }

    /*
     * Only init the socket once
     */
#ifdef WORKING_IPV6
    if (res->ai_addr->sa_family == AF_INET6 && not_init6 == 1) {
	uid_t euid;
	dgram_zero(&netfd6.dgram);
	
	euid = geteuid();
	set_root_privs(1);
	dgram_bind(&netfd6.dgram, res->ai_addr->sa_family, &port);
	set_root_privs(0);
	netfd6.handle = NULL;
	netfd6.pkt.body = NULL;
	netfd6.recv_security_ok = &bsd_recv_security_ok;
	netfd6.prefix_packet = &bsd_prefix_packet;
	/*
	 * We must have a reserved port.  Bomb if we didn't get one.
	 */
	if (port >= IPPORT_RESERVED) {
	    security_seterror(&bh->sech,
		"unable to bind to a reserved port (got port %u)",
		(unsigned int)port);
	    (*fn)(arg, &bh->sech, S_ERROR);
	    return;
	}
	not_init6 = 0;
	bh->udp = &netfd6;
    }
#endif

    if (res->ai_addr->sa_family == AF_INET && not_init4 == 1) {
	dgram_zero(&netfd4.dgram);

	set_root_privs(1);
	dgram_bind(&netfd4.dgram, res->ai_addr->sa_family, &port);
	set_root_privs(0);
	netfd4.handle = NULL;
	netfd4.pkt.body = NULL;
	netfd4.recv_security_ok = &bsd_recv_security_ok;
	netfd4.prefix_packet = &bsd_prefix_packet;
	/*
	 * We must have a reserved port.  Bomb if we didn't get one.
	 */
	if (port >= IPPORT_RESERVED) {
	    security_seterror(&bh->sech,
		_("unable to bind to a reserved port (got port %u)"),
		(unsigned int)port);
	    (*fn)(arg, &bh->sech, S_ERROR);
	    amfree(canonname);
	    return;
	}
	not_init4 = 0;
	bh->udp = &netfd4;
    }

#ifdef WORKING_IPV6
    if (res->ai_addr->sa_family == AF_INET6)
	bh->udp = &netfd6;
    else
#endif
	bh->udp = &netfd4;

    auth_debug(1, _("Resolved hostname=%s\n"), canonname);
    if ((se = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL)
	port = AMANDA_SERVICE_DEFAULT;
    else
	port = (in_port_t)ntohs(se->s_port);
    amanda_gettimeofday(&sequence_time);
    sequence = (int)sequence_time.tv_sec ^ (int)sequence_time.tv_usec;
    handle=alloc(15);
    g_snprintf(handle,14,"000-%08x", newhandle++);
    if (udp_inithandle(bh->udp, bh, canonname,
		       (struct sockaddr_storage *)res->ai_addr, port,
		       handle, sequence) < 0) {
	(*fn)(arg, &bh->sech, S_ERROR);
	amfree(bh->hostname);
	amfree(bh);
    } else {
	(*fn)(arg, &bh->sech, S_OK);
    }
    amfree(handle);
    amfree(canonname);

    if (res) freeaddrinfo(res);
}

/*
 * Setup to accept new incoming connections
 */
static void
bsdudp_accept(
    const struct security_driver *driver,
    char *      (*conf_fn)(char *, void *),
    int		in,
    int		out,
    void	(*fn)(security_handle_t *, pkt_t *),
    void       *datap)
{
    (void)driver;	/* Quiet unused parameter warning */
    (void)out;		/* Quiet unused parameter warning */
    (void)conf_fn;
    (void)datap;

    assert(in >= 0 && out >= 0);
    assert(fn != NULL);

    /*
     * We assume in and out point to the same socket, and just use
     * in.
     */
    dgram_socket(&netfd4.dgram, in);
    dgram_socket(&netfd6.dgram, in);

    /*
     * Assign the function and return.  When they call recvpkt later,
     * the recvpkt callback will call this function when it discovers
     * new incoming connections
     */
    netfd4.accept_fn = fn;
    netfd4.recv_security_ok = &bsd_recv_security_ok;
    netfd4.prefix_packet = &bsd_prefix_packet;
    netfd4.driver = &bsdudp_security_driver;


    udp_addref(&netfd4, &udp_netfd_read_callback);
}

/*
 * Frees a handle allocated by the above
 */
static void
bsdudp_close(
    void *cookie)
{
    struct sec_handle *bh = cookie;

    if(bh->proto_handle == NULL) {
	return;
    }

    auth_debug(1, _("bsdudp: close handle '%s'\n"), bh->proto_handle);

    udp_recvpkt_cancel(bh);
    if(bh->next) {
	bh->next->prev = bh->prev;
    }
    else {
	if (!not_init6 && netfd6.bh_last == bh)
	    netfd6.bh_last = bh->prev;
	else
	    netfd4.bh_last = bh->prev;
    }
    if(bh->prev) {
	bh->prev->next = bh->next;
    }
    else {
	if (!not_init6 && netfd6.bh_first == bh)
	    netfd6.bh_first = bh->next;
	else
	    netfd4.bh_first = bh->next;
    }

    amfree(bh->proto_handle);
    amfree(bh->hostname);
    amfree(bh);
}

