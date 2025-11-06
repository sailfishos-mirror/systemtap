/* COVERAGE: file_setattr */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

struct file_attr {
	unsigned long long fa_xflags;	/* xflags field value (get/set) */
	unsigned int fa_extsize;	/* extsize field value (get/set)*/
	unsigned int fa_nextents;	/* nextents field value (get)   */
	unsigned int fa_projid;	/* project identifier (get/set) */
	unsigned int fa_cowextsize;	/* CoW extsize field value (get/set) */
};

#ifdef __NR_file_setattr
static long my_file_setattr(int dfd, const char *filename,
                            struct file_attr *ufattr,
                            size_t usize, int at_flags)
{
    // PRE_REG_READ5(int, "file_setattr", int, dfd, const char*, filename,
    //               struct file_attr *, ufattr, vki_size_t, usize, int, at_flags);

    return syscall(__NR_file_setattr, dfd, filename, ufattr, usize, at_flags);
}
#endif

int main(void)
{
// kernel-headers-6.18.0-0.rc2.23.fc44.x86_64 does have __NR_file_setattr
// kernel-headers-6.16.2-100.fc41.x86_64 noes not.
#ifdef __NR_file_setattr
    int fd;
    struct file_attr fa;
    char *good = "testfile";
    char *bad = "nonexistent";
    int ret;

    /* Prepare a file */
    fd = open(good, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        perror("ERROR: Failed preparing test file");
        return 1;
    }

    my_file_setattr(-1, (const char*)-1, (struct file_attr *)-1, -1, -1);
    //staptest// file_setattr (-1, -1, 0x[f]+, 0x[f]+, 0x[f]+, 4294967295, 4294967295) = -NNNN (EINVAL)

    my_file_setattr(AT_FDCWD, bad, &fa, sizeof(fa), 0);
    //staptest// file_setattr (-100, AT_FDCWD, XXXX, "nonexistent", XXXX, NNNN, NNNN) = -2 (ENOENT)

    my_file_setattr(AT_FDCWD, NULL, &fa, sizeof(fa), 0);
    //staptest// file_setattr (-100, AT_FDCWD, 0x0, 0x0, XXXX, NNNN, 0) = -14 (EFAULT)

    my_file_setattr(AT_FDCWD, good, NULL, sizeof(fa), 0);
    //staptest// file_setattr (-100, AT_FDCWD, XXXX, "testfile", 0x0, NNNN, 0) = -14 (EFAULT)

    my_file_setattr(AT_FDCWD, good, &fa, 0, 0);
    //staptest// file_setattr (-100, AT_FDCWD, XXXX, "testfile", XXXX, 0, 0) = -22 (EINVAL)

    memset(&fa, 0, sizeof(fa));
    
    fa = (struct file_attr){0, 0, 0, 0, 1};
    ret = my_file_setattr(AT_FDCWD, good, &fa, sizeof(fa), 0);
    if (ret != 0) {
        printf("ERROR: file_setattr() failed unexpectedly\n");
        return 1;
    }
    //staptest// file_setattr (-100, AT_FDCWD, XXXX, "testfile", XXXX, NNNN, 0) = 0

    close(fd);
    unlink(good);
#endif
    return 0;
}
