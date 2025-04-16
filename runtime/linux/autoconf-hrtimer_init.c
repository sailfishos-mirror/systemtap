#include <linux/hrtimer.h>

void foo(void) {
  hrtimer_init(&stp->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  stp->hrtimer.function = function;
}
