/* COVERAGE: gettimeofday settimeofday clock_gettime clock_settime clock_adjtime clock_getres clock_nanosleep time stime */

#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/timex.h>
#include <string.h>
#include <sys/mman.h>

int main()
{
  int t;
  struct timeval tv;
  struct timespec ts;
  struct timex tx;
  time_t tt;

  mlockall(MCL_CURRENT);

#ifdef SYS_time
  syscall(SYS_time, &tt);
  //staptest// time (XXXX) = NNNN
  
  syscall(SYS_time, NULL);
  //staptest// time (0x[0]+) = NNNN

  syscall(SYS_time, -1);
#ifdef __s390__
  //staptest// time (0x[7]?[f]+) = -NNNN
#else
  //staptest// time (0x[f]+) = -NNNN
#endif
#endif

  t = syscall(SYS_gettimeofday, &tv, NULL);
  //staptest// gettimeofday (XXXX, 0x[0]+) = 0

// The stime() was deprecated, and with modern glibc (glibc-2.34-88.el9.ppc64le)
// isn't avail even with feature macros.  Glibc upstream commit 12cbde1dae6fa4 .
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__) && __GLIBC__ <= 2 && __GLIBC_MINOR__ <= 30
#ifdef __NR_stime
  tt = tv.tv_sec;
  stime(&tt);
  //staptest// stime (XXXX) = NNNN

  stime((time_t *)-1);
#ifdef __s390__
  //staptest// stime (0x[7]?[f]+) = -NNNN
#else
  //staptest// stime (0x[f]+) = -NNNN
#endif
#endif
#endif

  settimeofday(&tv, NULL);
  //staptest// [[[[settimeofday (\[NNNN.NNNN\], NULL)!!!!clock_settime (CLOCK_REALTIME, \[NNNN.NNNN\])]]]] = NNNN

// With modern glibc, the first arg of settimeofday is marked as restrict,
// and this sub testcase SEGVs.
#if defined(__GLIBC__) && defined(__GLIBC_MINOR__) && __GLIBC__ <= 2 && __GLIBC_MINOR__ <= 30
  settimeofday((struct timeval *)-1, NULL);
#ifdef __s390__
  //staptest// settimeofday (0x[7]?[f]+, NULL) = -NNNN (EFAULT)
#else
  //staptest// settimeofday (0x[f]+, NULL) = -NNNN (EFAULT)
#endif
#endif

  syscall(SYS_settimeofday, &tv, (struct timezone *)-1);
#ifdef __s390__
  //staptest// settimeofday (\[NNNN.NNNN\], 0x[7]?[f]+) = -NNNN (EFAULT)
#else
  //staptest// settimeofday (\[NNNN.NNNN\], 0x[f]+) = -NNNN (EFAULT)
#endif

  syscall(SYS_gettimeofday, -1, NULL);
#ifdef __s390__
  //staptest// gettimeofday (0x[7]?[f]+, 0x[0]+) = -NNNN (EFAULT)
#else
  //staptest// gettimeofday (0x[f]+, 0x[0]+) = -NNNN (EFAULT)
#endif

  syscall(SYS_gettimeofday, &tv, -1);
#ifdef __s390__
  //staptest// gettimeofday (XXXX, 0x[7]?[f]+) = -NNNN (EFAULT)
#else
  //staptest// gettimeofday (XXXX, 0x[f]+) = -NNNN (EFAULT)
#endif

  syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
  //staptest// clock_gettime (CLOCK_REALTIME, XXXX) = 0

  syscall(SYS_clock_settime, CLOCK_REALTIME, &ts);
  //staptest// clock_settime (CLOCK_REALTIME, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_REALTIME, &ts);
  //staptest// clock_getres (CLOCK_REALTIME, XXXX) = 0

// On s390x, the following probe doesn't get the return hit, skip it.
#if !defined(__s390__)
  syscall(SYS_clock_getres, -1, &ts);
  //staptest// clock_getres (0xffffffff, XXXX) = -NNNN (EINVAL)
#endif

  syscall(SYS_clock_getres, CLOCK_REALTIME, -1);
#ifdef __s390__
  //staptest// clock_getres (CLOCK_REALTIME, 0x[7]?[f]+) = -NNNN (EFAULT)
#else
  //staptest// clock_getres (CLOCK_REALTIME, 0x[f]+) = -NNNN (EFAULT)
#endif

  syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
  //staptest// clock_gettime (CLOCK_MONOTONIC, XXXX) = 0

  syscall(SYS_clock_settime, CLOCK_MONOTONIC, &ts);
  //staptest// clock_settime (CLOCK_MONOTONIC, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_MONOTONIC, &ts);
  //staptest// clock_getres (CLOCK_MONOTONIC, XXXX) = 0

  syscall(SYS_clock_gettime, CLOCK_PROCESS_CPUTIME_ID, &ts);
  //staptest// clock_gettime (CLOCK_PROCESS_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_settime, CLOCK_PROCESS_CPUTIME_ID, &ts);
  //staptest// clock_settime (CLOCK_PROCESS_CPUTIME_ID, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_PROCESS_CPUTIME_ID, &ts);
  //staptest// clock_getres (CLOCK_PROCESS_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_gettime, CLOCK_THREAD_CPUTIME_ID, &ts);
  //staptest// clock_gettime (CLOCK_THREAD_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_settime, CLOCK_THREAD_CPUTIME_ID, &ts);
  //staptest// clock_settime (CLOCK_THREAD_CPUTIME_ID, \[NNNN.NNNN\]) =

  syscall(SYS_clock_getres, CLOCK_THREAD_CPUTIME_ID, &ts);
  //staptest// clock_getres (CLOCK_THREAD_CPUTIME_ID, XXXX) =

  syscall(SYS_clock_gettime, CLOCK_REALTIME, &ts);
  //staptest// clock_gettime (CLOCK_REALTIME, XXXX) = 0

  ts.tv_sec++;
  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, TIMER_ABSTIME, &ts);
  //staptest// clock_nanosleep (CLOCK_REALTIME, TIMER_ABSTIME, \[NNNN.NNNN\], XXXX) = 0

  ts.tv_sec = 0;   ts.tv_nsec = 10000;  
  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, 0x0, &ts);
  //staptest// clock_nanosleep (CLOCK_REALTIME, 0x0, \[NNNN.NNNN\], XXXX) = 0
  
  syscall(SYS_clock_gettime, -1, &ts);
  //staptest// clock_gettime (0x[f]+, XXXX) = -NNNN (EINVAL)

  syscall(SYS_clock_gettime, CLOCK_REALTIME, -1);
#ifdef __s390__
  //staptest// clock_gettime (CLOCK_REALTIME, 0x[7]?[f]+) = -NNNN (EFAULT)
#else
  //staptest// clock_gettime (CLOCK_REALTIME, 0x[f]+) = -NNNN (EFAULT)
#endif

  syscall(SYS_clock_settime, -1, &ts);
  //staptest// clock_settime (0x[f]+, \[NNNN.NNNN\]) = -NNNN (EINVAL)

  syscall(SYS_clock_settime, CLOCK_MONOTONIC, -1);
#ifdef __s390__
  //staptest// clock_settime (CLOCK_MONOTONIC, 0x[7]?[f]+) = -NNNN
#else
  //staptest// clock_settime (CLOCK_MONOTONIC, 0x[f]+) = -NNNN
#endif

  ts.tv_sec = 0;   ts.tv_nsec = 10000;  
  syscall(SYS_clock_nanosleep, -1, 0x0, &ts);
  //staptest// clock_nanosleep (0x[f]+, 0x0, \[NNNN.NNNN\], XXXX) = -NNNN
  ts.tv_sec = 0;   ts.tv_nsec = 10000;  

  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, -1, &ts);
  //staptest// clock_nanosleep (CLOCK_REALTIME, 0x[f]+, \[NNNN.NNNN\], XXXX) = 0
  ts.tv_sec = 0;   ts.tv_nsec = 10000;  

  syscall(SYS_clock_nanosleep, CLOCK_REALTIME, 0x0, -1);
#ifdef __s390__
  //staptest// clock_nanosleep (CLOCK_REALTIME, 0x0, 0x[7]?[f]+, XXXX) = -NNNN (EFAULT)
#else
  //staptest// clock_nanosleep (CLOCK_REALTIME, 0x0, 0x[f]+, XXXX) = -NNNN (EFAULT)
#endif

// The following SEGVs if compiled as a 32-on-64 bit binary on x86_64 in valid_timex_to_timex64()
#if __WORDSIZE == 64
  adjtimex((struct timex *)-1);
#ifdef __s390__
  //staptest// [[[[adjtimex (!!!!clock_adjtime (CLOCK_REALTIME, ]]]]0x[7]?[f]+) = NNNN
#else
  //staptest// [[[[adjtimex (!!!!clock_adjtime (CLOCK_REALTIME, ]]]]0x[f]+) = NNNN
#endif
#endif

#ifdef __NR_clock_adjtime
  memset(&tx, 0, sizeof(tx));
  tx.modes = ADJ_STATUS;
  adjtimex(&tx);
  tx.modes = ADJ_FREQUENCY;

  syscall(__NR_clock_adjtime, CLOCK_REALTIME, &tx);
  //staptest// clock_adjtime (CLOCK_REALTIME, \{ADJ_FREQUENCY, constant=NNNN, esterror=NNNN, freq=NNNN, maxerror=NNNN, offset=NNNN, precision=NNNN, status=NNNN, tick=NNNN, tolerance=NNNN\}) = NNNN

  syscall(__NR_clock_adjtime, (int)-1, &tx);
  //staptest// clock_adjtime (0xffffffff, \{[^\}]*\}) = -NNNN

  syscall(__NR_clock_adjtime, CLOCK_REALTIME, (struct timex *)-1);
#ifdef __s390__
  //staptest// clock_adjtime (CLOCK_REALTIME, 0x[7]?[f]+) = -NNNN
#else
  //staptest// clock_adjtime (CLOCK_REALTIME, 0x[f]+) = -NNNN
#endif
#endif

  return 0;
}
