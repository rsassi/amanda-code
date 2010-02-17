/*
 * Copyright (c) 1998,1999,2000
 *	Traakan, Inc., Los Altos, CA
 *	All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Project:  NDMJOB
 * Ident:    $Id: $
 *
 * Description:
 *
 */


#define GLOBAL
#include "ndmjob.h"
#include "debug.h"
#include "util.h"


int
main (int ac, char *av[])
{
	int rc;

	set_pname("amndmjob");
	dbopen(DBG_SUBDIR_CLIENT);
	config_init(0, NULL);

	NDMOS_MACRO_ZEROFILL(&the_session);
	d_debug = -1;

	/* ready the_param early so logging works during process_args() */
	NDMOS_MACRO_ZEROFILL (&the_param);
	the_param.log.deliver = ndmjob_log_deliver;
	the_param.log_level = 0;
	the_param.log_tag = "SESS";

#ifndef NDMOS_OPTION_NO_CONTROL_AGENT
	b_bsize = 20;
	index_fp = stderr;
	o_tape_addr = -1;
	o_from_addr = -1;
	o_to_addr = -1;
	p_ndmp_port = NDMPPORT;
#endif /* !NDMOS_OPTION_NO_CONTROL_AGENT */

	process_args (ac, av);

	if (the_param.log_level < d_debug)
		the_param.log_level = d_debug;
	if (the_param.log_level < v_verbose)
		the_param.log_level = v_verbose;
	the_param.config_file_name = o_config_file;

	if (the_mode == NDM_JOB_OP_DAEMON || the_mode == NDM_JOB_OP_TEST_DAEMON) {
		the_session.param = the_param;

		if (n_noop) {
			dump_settings();
			return 0;
		}
		ndma_daemon_session (&the_session, p_ndmp_port, the_mode == NDM_JOB_OP_TEST_DAEMON);
		return 0;
	}

#ifndef NDMOS_OPTION_NO_CONTROL_AGENT
	the_session.control_acb.swap_connect = (o_swap_connect != 0);

	build_job();		/* might not return */

	the_session.param = the_param;
	the_session.control_acb.job = the_job;

	if (n_noop) {
		dump_settings();
		return 0;
	}

	start_index_file ();

	rc = ndma_client_session (&the_session);

	sort_index_file ();

	if (rc == 0)
	    ndmjob_log (1, "Operation complete");
	else
	    ndmjob_log (1, "Operation complete but had problems.");
#endif /* !NDMOS_OPTION_NO_CONTROL_AGENT */

	dbclose();
	return 0;
}


