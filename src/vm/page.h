#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <hash.h>
#include "filesys/off_t.h"

/* Page. */
struct page
  {
    void *addr;                         /* Virtual address. */
    struct file *file;                  /* Loaded file. */
    off_t file_ofs;                     /* Offset of the file. */
    uint32_t file_read_bytes;           /* Number of read bytes from file. */
    bool file_writable;                 /* File is writable. */
    bool valid;                         /* Frame is not swapped out. */
    size_t swap_idx;                    /* Swap index of the frame. */
    struct hash_elem hash_elem;         /* Hash table element. */
  };

bool page_init (void);
struct page *page_insert (const void *address);
struct page *page_find (const void *address);
void page_clear (void);
bool page_load_swap (struct page *page);
bool page_load_file (struct page *page);
bool page_load_zero (struct page *page);

#endif /* vm/page.h */
