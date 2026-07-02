#include <linux/tracepoint.h>
#include <trace/events/percpu.h>

/*
 * SystemTap tracepoint probes use DWARF-derived parameter types.  When
 * the kernel uses address-space qualifiers (e.g. __seg_gs via
 * __percpu) that DWARF omits, check_trace_callback_type_* rejects our
 * callback signature at module build time.
 */
static void stap_tracepoint_typecheck_probe(void *__data,
					    unsigned long call_site,
					    bool reserved, bool is_atomic,
					    size_t size, size_t align,
					    void *base_addr, int off,
					    void *ptr, size_t bytes_alloc,
					    gfp_t gfp_flags)
{
	(void) call_site;
	(void) reserved;
	(void) is_atomic;
	(void) size;
	(void) align;
	(void) base_addr;
	(void) off;
	(void) ptr;
	(void) bytes_alloc;
	(void) gfp_flags;
}

void foo(void);

void
foo(void)
{
	check_trace_callback_type_percpu_alloc_percpu(stap_tracepoint_typecheck_probe);
}
