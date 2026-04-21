#include <linux/kernel.h>
#include <linux/list.h>

struct foo {
        struct hlist_node foo_a;
};

struct hlist_head *h;

void foo (void);
void foo (void)
{
        __attribute__((unused)) struct hlist_node *n;
        __attribute__((unused)) struct foo *fooptr;
        
        hlist_for_each_entry(fooptr, n, h, foo_a) {
                ;
        }
}
