#include <linux/mm.h>

//
// The following kernel commit changed the get_user_pages() function
// signature for linux-7.1:
//
// commit (linux-7.1 mm/gup changes)
//
//     mm/gup: remove mm_struct pointer from get_user_pages()
//
// This changed the function signature from:
//
// long get_user_pages(struct mm_struct *mm,
//                     unsigned long start, unsigned long nr_pages,
//                     unsigned int gup_flags, struct page **pages,
//                     struct vm_area_struct **vmas);
//
// to:
//
// long get_user_pages(unsigned long start, unsigned long nr_pages,
//                     unsigned int gup_flags, struct page **pages);
//

long gup_wrapper(unsigned long start, unsigned long nr_pages,
		 unsigned int gup_flags, struct page **pages);

long gup_wrapper(unsigned long start, unsigned long nr_pages,
		 unsigned int gup_flags, struct page **pages)
{
    return get_user_pages(start, nr_pages, gup_flags, pages);
}
