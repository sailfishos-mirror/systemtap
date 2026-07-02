#include <linux/tracepoint.h>

/*
 * Kernel 6.16+ DECLARE_TRACE() appends _tp to the internal tracepoint
 * name while the registration string stays unprefixed (e.g. "pelt_cfs").
 * TRACE_EVENT() uses DECLARE_TRACE_EVENT() and is unaffected.
 */
#ifndef DECLARE_TRACE_EVENT
#error DECLARE_TRACE() does not append _tp suffix
#endif

void foo(void);

void
foo(void)
{
}
