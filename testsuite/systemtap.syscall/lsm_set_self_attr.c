/* COVERAGE: lsm_set_self_attr */

#include <sys/syscall.h>      /* Definition of SYS_* constants */
#include <unistd.h>           /* for syscall() */
#include <linux/lsm.h>        /* struct lsm_ctx */
#include <errno.h>            /* for EINVAL etc */
#include <stdlib.h>           /* for calloc */

#ifdef __NR_lsm_set_self_attr
// SYSCALL_DEFINE4(lsm_set_self_attr, unsigned int, attr, struct lsm_ctx __user *,
//                 ctx, u32 __user *, size, u32, flags)
static long my_lsm_set_self_attr(unsigned int attr, struct lsm_ctx *ctx,
                                 unsigned int *size, unsigned int flags)
{
    return syscall(SYS_lsm_set_self_attr, attr, ctx, size, flags);
}
#endif

#define LSM_CTX_SIZE_DEFAULT 128 /* struct lsm_ctx doesn't have
                                  * a fixed size because of it's last member
                                  * __u8 ctx[] __counted_by(ctx_len); */

int main()
{
#ifdef __NR_lsm_set_self_attr
    struct lsm_ctx ctx;
    static unsigned int ctx_size, ctx_size_small;
    my_lsm_set_self_attr(-1, (struct lsm_ctx *)-1, (unsigned int *)-1, -1);
    //staptest// lsm_set_self_attr (4294967295, 0x[f]+, 0x[f]+, 4294967295) = NNNN (EINVAL)
    my_lsm_set_self_attr(LSM_ATTR_CURRENT, &ctx, NULL, 0);
    //staptest// lsm_set_self_attr (100, XXXX, 0x0, 0) = NNNN (EINVAL)
    my_lsm_set_self_attr(LSM_ATTR_CURRENT, &ctx, &ctx_size, LSM_FLAG_SINGLE | (LSM_FLAG_SINGLE<<1));
    //staptest// lsm_set_self_attr (100, XXXX, XXXX, 3) = NNNN (EINVAL)
    my_lsm_set_self_attr(LSM_ATTR_CURRENT, &ctx, &ctx_size_small, 0);
    //staptest// lsm_set_self_attr (100, XXXX, XXXX, 0) = NNNN (E2BIG)
    my_lsm_set_self_attr(LSM_ATTR_CURRENT, &ctx, &ctx_size, LSM_FLAG_SINGLE);
    //staptest// lsm_set_self_attr (100, XXXX, XXXX, 1) = NNNN (EINVAL)
    my_lsm_set_self_attr(LSM_ATTR_CURRENT|LSM_ATTR_PREV, &ctx, &ctx_size, 0);
    //staptest// lsm_set_self_attr (108, XXXX, XXXX, 0) = NNNN (E2BIG)
#endif
    return 0;
}
