#include <linux/regset.h>

int foobar_get2(int n);
int foobar_get2(int n) {
  const struct user_regset_view *rsv = task_user_regset_view(current);
  const struct user_regset *rs = & rsv->regsets[0];
  unsigned long value;
  return rsv->n + n + regset_get(current, rs, sizeof(value), &value);
}
