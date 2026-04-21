/* Some kernels have warning fields in stacktrace_ops. */
#include <linux/sched.h>
#include <asm/stacktrace.h>

void foo (void);

void foo (void)
{
  __attribute__((unused)) struct stacktrace_ops t;
  t.warning = 0;
}
