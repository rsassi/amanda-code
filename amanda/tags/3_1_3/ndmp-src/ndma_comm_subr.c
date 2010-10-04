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


#include "ndmagents.h"

void
ndmalogf (struct ndm_session *sess, char *tag, int level, char *fmt, ...)
{
	va_list		ap;

	if (sess->param.log_level < level)
		return;

	if (!tag) tag = sess->param.log_tag;
	if (!tag) tag = "???";

	va_start (ap, fmt);
	ndmlogfv (&sess->param.log, tag, level, fmt, ap);
	va_end (ap);
}


void
ndmalogfv (struct ndm_session *sess, char *tag,
  int level, char *fmt, va_list ap)
{
	if (sess->param.log_level < level)
		return;

	if (!tag) tag = sess->param.log_tag;
	if (!tag) tag = "???";

	ndmlogfv (&sess->param.log, tag, level, fmt, ap);
}


#if 0
#ifndef NDMOS_OPTION_NO_NDMP2
char *
ndma_log_dbg_tag (ndmp2_debug_level lev)
{
	switch (lev) {
	case NDMP2_DBG_USER_INFO:	return "ui";
	case NDMP2_DBG_USER_SUMMARY:	return "us";
	case NDMP2_DBG_USER_DETAIL:	return "ud";
	case NDMP2_DBG_DIAG_INFO:	return "di";
	case NDMP2_DBG_DIAG_SUMMARY:	return "ds";
	case NDMP2_DBG_DIAG_DETAIL:	return "dd";
	case NDMP2_DBG_PROG_INFO:	return "pi";
	case NDMP2_DBG_PROG_SUMMARY:	return "ps";
	case NDMP2_DBG_PROG_DETAIL:	return "pd";
	default:			return "??";
	}
}
#endif /* !NDMOS_OPTION_NO_NDMP2 */
#endif

