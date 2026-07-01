#include <linux/filelock.h>

void foo(void);

void
foo(void)
{
	struct file_lock *fl = (struct file_lock *)0;

	(void) fl->c.flc_type;
}
