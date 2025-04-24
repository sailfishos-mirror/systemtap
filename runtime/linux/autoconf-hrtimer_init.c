#include <linux/hrtimer.h>

extern void foo(void);

void foo(void) {
  struct hrtimer timer;
  hrtimer_init(&timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
}
