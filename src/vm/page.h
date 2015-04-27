#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <hash.h>

/* Page. */
struct page
  {
    void *addr;                         /* Virtual address. */
    struct hash_elem hash_elem;         /* Hash table element. */
  };

bool page_init (void);
struct page *page_insert (const void *address);
struct page *page_find (const void *address);
void page_clear (void);

#endif /* vm/page.h */
