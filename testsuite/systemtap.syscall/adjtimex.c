/* COVERAGE: adjtimex */

#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
#include <sys/timex.h>
#include <sys/syscall.h>

int main()
{
    static struct timex ts;

    memset(&ts, 0, sizeof(ts));
    ts.modes = ADJ_STATUS;
    adjtimex(&ts);
    //staptest// [[[[adjtimex (!!!!clock_adjtime (CLOCK_REALTIME, ]]]]\{ADJ_STATUS, constant=0, esterror=0, freq=0, maxerror=0, offset=NNNN, precision=0, status=0, tick=NNNN, tolerance=0\}) = NNNN

    // The glibc wrapper would SEGV in the following case, so we resort to
    // direct syscall invocation
    syscall(__NR_adjtimex, (struct timex *)-1);
#ifdef __s390__
    //staptest// adjtimex (0x[7]?[f]+) = NNNN
#else
    //staptest// adjtimex (0x[f]+) = NNNN
#endif

    return 0;

}

