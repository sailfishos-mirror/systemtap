#include <unistd.h>
#include <stdlib.h>

struct foo
{
  int bar;
};

static struct foo foo;

int
sub(int a)
{
  int i, j;
  for (i = 0; i < 1000; i++)
    for (j = 0; j < 1000; j++)
      a += i + j + rand();
  return a;
}

int
main (int argc, char **argv)
{
  foo.bar = 41 + argc; /* 42 */
  /* Prevent compiler from delaying or optimizing away the memory write */
  asm volatile ("" : : "m" (foo.bar));
  sub(argc);
  return 0;
}
