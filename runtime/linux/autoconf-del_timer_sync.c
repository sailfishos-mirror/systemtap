#include <linux/timer.h>

void foo(void) {
  struct timer_list *t=NULL;
  timer_delete_sync(t);
}
