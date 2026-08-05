/* LD_PRELOAD helper: force PTHREAD_MUTEX_ERRORCHECK on every
 * pthread_mutex_init.  Used to tell Helgrind false positives apart from
 * real same-thread double-locks on non-recursive mutexes (see comments
 * in parallelism-helgrind-asio.supp).
 *
 * Build:
 *   gcc -shared -fPIC -o errorcheck_mutex.so \
 *       parallelism-helgrind-errorcheck-mutex.c -ldl
 *
 * Not wired into dejagnu; keep next to the Helgrind .exp / .supp.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>

int
pthread_mutex_init (pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
  static int (*real) (pthread_mutex_t *, const pthread_mutexattr_t *);
  pthread_mutexattr_t tmp;
  pthread_mutexattr_t *ap = (pthread_mutexattr_t *) a;
  int rc;

  if (!real)
    real = dlsym (RTLD_NEXT, "pthread_mutex_init");

  if (!a)
    {
      pthread_mutexattr_init (&tmp);
      ap = &tmp;
    }
  pthread_mutexattr_settype (ap, PTHREAD_MUTEX_ERRORCHECK);
  rc = real (m, ap);
  if (!a)
    pthread_mutexattr_destroy (&tmp);
  return rc;
}
