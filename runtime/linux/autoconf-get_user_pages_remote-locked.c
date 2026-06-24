#include <linux/mm.h>

//
// The following kernel commit changed the get_user_pages_remote()
// function signature for linux-7.1:
//
// commit (linux-7.1 mm/gup changes)
//
//    mm/gup: remove vmas parameter from get_user_pages_remote()
//
// This changed the function signature from:
//
// long get_user_pages_remote(struct mm_struct *mm,
//                             unsigned long start, unsigned long nr_pages,
//                             unsigned int gup_flags, struct page **pages,
//                             struct vm_area_struct **vmas, int *locked);
//
// to:
//
// long get_user_pages_remote(struct mm_struct *mm,
//                             unsigned long start, unsigned long nr_pages,
//                             unsigned int gup_flags, struct page **pages,
//                             int *locked);
//

long gupr_wrapper(struct mm_struct *mm,
		  unsigned long start, unsigned long nr_pages,
		  unsigned int gup_flags, struct page **pages,
		  int *locked);

long gupr_wrapper(struct mm_struct *mm,
		  unsigned long start, unsigned long nr_pages,
		  unsigned int gup_flags, struct page **pages,
		  int *locked)
{
    return get_user_pages_remote(mm, start, nr_pages, gup_flags,
				 pages, locked);
}
