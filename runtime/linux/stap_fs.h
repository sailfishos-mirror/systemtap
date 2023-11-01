#ifndef LINUX_STAP_FS_H
#define LINUX_STAP_FS_H

#include <linux/namei.h>

static inline char *
stap_real_path(const char *pathname, size_t *len_ptr)
{
	int rc;
	char *ret;
	char *path_buf = NULL;
	char *p;
	struct path path;

	*len_ptr = 0;

	path_buf = _stp_kmalloc(PATH_MAX);
	if (unlikely(path_buf == NULL)) {
		ret = ERR_PTR(-ENOMEM);
		goto out;
	}

	rc = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (unlikely(rc != 0)) {
		_stp_error("Couldn't resolve target program file path '%s': %d\n",
			   pathname, rc);
		ret = ERR_PTR(rc);
		goto out_with_buf;
	}

	p = d_path(&path, path_buf, PATH_MAX);
	if (unlikely(p == NULL)) {
		ret = ERR_PTR(-EINVAL);
		goto out_with_path;
	}

	if (unlikely(IS_ERR(p))) {
		_stp_error("d_path() failed for path '%s': %ld\n", pathname,
			   PTR_ERR(p));
		ret = p;
		goto out_with_path;
	}

#if 0
	pr_warn("path buf: '%s' -> '%s'\n", pathname, p);
#endif

	if (strcmp(p, pathname) != 0) {  /* found a symlink */
		char *q;

		size_t len = strlen(p);
		if (unlikely(len == 0)) {
			ret = ERR_PTR(-EINVAL);
			goto out_with_path;
		}

		q = _stp_kmalloc(len + 1);
		if (unlikely(q == NULL)) {
			ret = ERR_PTR(-ENOMEM);
			goto out_with_path;
		}

		memcpy(q, p, len + 1);
		*len_ptr = len;

		ret = q;
		goto out_with_path;
	}

	ret = NULL;

out_with_path:

	path_put(&path);

out_with_buf:

	if (likely(path_buf != NULL))
		_stp_kfree(path_buf);
out:

    return ret;
}

#endif  /* LINUX_STAP_FS_H */
