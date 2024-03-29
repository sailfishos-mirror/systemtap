/*
 * The kernel's access_process_vm is not exported in kernel.org kernels, although
 * some distros export it on some architectures.  To workaround this inconsistency,
 * we copied and pasted it here.  Fortunately, everything it calls is exported.
 */
#include "stap_mmap_lock.h"
#include <linux/sched.h>
#if defined(STAPCONF_LINUX_SCHED_HEADERS)
#include <linux/sched/mm.h>
#endif
#include <linux/pagemap.h>
#include <asm/cacheflush.h>

static int
__access_process_vm_ (struct task_struct *tsk, unsigned long addr, void *buf,
		      int len, int write,
		      void (*writer) (struct vm_area_struct * vma,
				      struct page * page, unsigned long vaddr,
				      void *dst, void *src, int len),
		      void (*reader) (struct vm_area_struct * vma,
				      struct page * page, unsigned long vaddr,
				      void *dst, void *src, int len))
{
  struct mm_struct *mm;
  struct vm_area_struct *vma;
  struct page *page;
  void *old_buf = buf;

  mm = get_task_mm (tsk);
  if (!mm)
    return 0;

  mmap_read_lock (mm);
  /* ignore errors, just check how much was successfully transferred */
  while (len)
    {
      int bytes, ret, offset;
      void *maddr;

#ifdef STAPCONF_GET_USER_PAGES_REMOTE
#if defined(STAPCONF_GET_USER_PAGE_VMA_REMOTE)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      page = get_user_page_vma_remote (mm, addr, flags, &vma);
      ret = !IS_ERR_OR_NULL(page);
#elif defined(STAPCONF_GET_USER_PAGES_REMOTE_NOTASK_STRUCT)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      ret = get_user_pages_remote (mm, addr, 1, flags, &page, &vma, NULL);
#elif defined(STAPCONF_GET_USER_PAGES_REMOTE_FLAGS_LOCKED)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      ret = get_user_pages_remote (tsk, mm, addr, 1, flags, &page, &vma, NULL);
#elif defined(STAPCONF_GET_USER_PAGES_REMOTE_FLAGS)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      ret = get_user_pages_remote (tsk, mm, addr, 1, flags, &page, &vma);
#else
      ret = get_user_pages_remote (tsk, mm, addr, 1, write, 1, &page, &vma);
#endif
#else /* !STAPCONF_GET_USER_PAGES_REMOTE* */
#if defined(STAPCONF_GET_USER_PAGES_NOTASK_STRUCT)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      ret = get_user_pages (mm, addr, 1, flags, &page, &vma);
#elif defined(STAPCONF_GET_USER_PAGES_FLAGS)
      unsigned int flags = FOLL_FORCE;
      if (write)
	  flags |= FOLL_WRITE;
      ret = get_user_pages (tsk, mm, addr, 1, flags, &page, &vma);
#else
      ret = get_user_pages (tsk, mm, addr, 1, write, 1, &page, &vma);
#endif
#endif
      if (ret <= 0)
	break;

      bytes = len;
      offset = addr & (PAGE_SIZE - 1);
      if (bytes > PAGE_SIZE - offset)
	bytes = PAGE_SIZE - offset;

      maddr = kmap (page);
      if (write)
	{
	  writer (vma, page, addr, maddr + offset, buf, bytes);
	  set_page_dirty_lock (page);
	}
      else
	{
	  reader (vma, page, addr, buf, maddr + offset, bytes);
	}
      kunmap (page);
      put_page (page);
      len -= bytes;
      buf += bytes;
      addr += bytes;
    }
  mmap_read_unlock(mm);
  mmput (mm);

  return buf - old_buf;
}

static void
copy_to_user_page_ (struct vm_area_struct *vma, struct page *page,
		    unsigned long vaddr, void *dst, void *src, int len)
{
  copy_to_user_page (vma, page, vaddr, dst, src, len);
}

static void
copy_from_user_page_ (struct vm_area_struct *vma, struct page *page,
		      unsigned long vaddr, void *dst, void *src, int len)
{
  copy_from_user_page (vma, page, vaddr, dst, src, len);
}

static int
__access_process_vm (struct task_struct *tsk, unsigned long addr, void *buf,
		     int len, int write)
{
  return __access_process_vm_ (tsk, addr, buf, len, write, copy_to_user_page_,
			       copy_from_user_page_);
}

/*  This simpler version does not flush the caches.  */

static void
copy_to_user_page_noflush (struct vm_area_struct *vma, struct page *page,
			   unsigned long vaddr, void *dst, void *src, int len)
{
  memcpy (dst, src, len);
}

static void
copy_from_user_page_noflush (struct vm_area_struct *vma, struct page *page,
			     unsigned long vaddr, void *dst, void *src,
			     int len)
{
  memcpy (dst, src, len);
}

static int
__access_process_vm_noflush (struct task_struct *tsk, unsigned long addr,
			     void *buf, int len, int write)
{
  return __access_process_vm_ (tsk, addr, buf, len, write,
			       copy_to_user_page_noflush,
			       copy_from_user_page_noflush);
}
