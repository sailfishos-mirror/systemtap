#include <linux/nsproxy.h>

long foo(void) {
    struct nsset set = {};
    return (long) &set.nsproxy;
}
