#include "sys/sdt.h"
#include <unistd.h>

void sleeper () {
  sleep(1);
}

int main () {
    /* Finite lifetime so a missed cleanup cannot leave orphans forever. */
    int i;
    for (i = 0; i < 600; i++) {
        sleeper();
        marker_here:
            STAP_PROBE(process_by_pid, while_start);
    }
    if (0) // suppress gcc warning about unused label
      goto marker_here;
    return 0;
}
