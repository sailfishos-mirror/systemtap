#include <linux/sched.h>

void foo (void);

void foo (void) {
    (void) MM_SHMEMPAGES;
}
