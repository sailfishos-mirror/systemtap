#include <linux/kernel.h>

int foo(void);

int foo(void) {
  return kernel_is_locked_down("something");
}
