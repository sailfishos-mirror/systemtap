/* Dyninst runtime stash for third-party ubacktraces captured by stapdyn.
 * Copyright (C) 2026 Red Hat Inc.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#ifndef _STP_DYNINST_UBACKTRACE_C_
#define _STP_DYNINST_UBACKTRACE_C_

/* Filled by stapdyn before firing a probe that may call print_ubacktrace*. */
static char _stp_dyninst_ubacktrace_buf[STAPDYN_UBACKTRACE_MAX];

void stp_dyninst_ubacktrace_set(const char *bt)
{
	if (!bt || !bt[0]) {
		_stp_dyninst_ubacktrace_buf[0] = '\0';
		return;
	}
	strlcpy(_stp_dyninst_ubacktrace_buf, bt,
		sizeof(_stp_dyninst_ubacktrace_buf));
}

static void _stp_dyninst_ubacktrace_print(void)
{
	if (_stp_dyninst_ubacktrace_buf[0])
		_stp_print(_stp_dyninst_ubacktrace_buf);
	else
		_stp_print("<no user backtrace>\n");
}

static void _stp_dyninst_ubacktrace_sprint(char *buf, size_t len)
{
	if (!buf || len == 0)
		return;
	if (_stp_dyninst_ubacktrace_buf[0])
		strlcpy(buf, _stp_dyninst_ubacktrace_buf, len);
	else
		buf[0] = '\0';
}

#endif /* _STP_DYNINST_UBACKTRACE_C_ */
