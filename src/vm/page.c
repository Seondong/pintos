#include "vm/page.h"
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <hash.h>
#include <user/syscall.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

static hash_hash_func page_hash;
static hash_less_func page_less;
static hash_action_func page_destructor;

/* Initializes the supplemental page table. */
bool
page_init (struct hash *page_table)
{
  return hash_init (page_table, page_hash, page_less, NULL);
}

/* Inserts a page with given ADDRESS into the supplemental page
   table.  If there is no such page, returns NULL. */
struct page *
page_insert (const void *address)
{
  struct page *p = (struct page *) malloc (sizeof (struct page));
  struct hash_elem *e;

  p->addr = (void *) address;
  p->loaded = true;
  p->mapid = MAP_FAILED;
  p->file = NULL;
  p->valid = true;
  e = hash_insert (&thread_current ()->page_table, &p->hash_elem);
  if (e != NULL)
    {
      free (p);
      return hash_entry (e, struct page, hash_elem);
    }
  return NULL;
}

/* Finds a page with the given ADDRESS from the page table. */
struct page *
page_find (struct hash *page_table, const void *address)
{
  struct page p;
  struct hash_elem *e;

  p.addr = (void *) address;
  e = hash_find (page_table, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Clears the page table. */
void
page_destroy (struct hash *page_table)
{
  hash_destroy (page_table, page_destructor);
}

/* Load the given PAGE from swap. */
bool
page_load_swap (struct page *page)
{
  struct thread *t = thread_current ();
  void *kpage = frame_alloc (page->addr, 0);
  bool success;

  ASSERT (!page->valid);

  swap_in (page, kpage);
  success = (pagedir_get_page (t->pagedir, page->addr) == NULL
             && pagedir_set_page (t->pagedir, page->addr, kpage, true));
  if (!success)
    {
      frame_free (kpage);
      return false;
    }
  pagedir_set_dirty (t->pagedir, page->addr, true);
  pagedir_set_accessed (t->pagedir, page->addr, true);
  page->valid = true;
  return true;
}

/* Load the given PAGE from a file. */
bool
page_load_file (struct page *page)
{
  struct thread *t = thread_current ();
  void *kpage;
  bool success;

  ASSERT (!page->loaded);
  ASSERT (page->file != NULL);

  if (page->file_read_bytes == 0)
    kpage = frame_alloc (page->addr, PAL_ZERO);
  else
    kpage = frame_alloc (page->addr, 0);

  if (kpage == NULL)
    return false;

  if (page->file_read_bytes > 0)
    {
      filesys_acquire ();
      if ((int) page->file_read_bytes != file_read_at (page->file, kpage,
                                                       page->file_read_bytes,
                                                       page->file_ofs))
        {
          filesys_release ();
          frame_free (kpage);
          return false;
        }
      filesys_release ();
      memset (kpage + page->file_read_bytes, 0, PGSIZE - page->file_read_bytes);
    }

  success = (pagedir_get_page (t->pagedir, page->addr) == NULL
             && pagedir_set_page (t->pagedir, page->addr, kpage,
                                  page->file_writable));
  if (!success)
    {
      frame_free (kpage);
      return false;
    }
  pagedir_set_accessed (t->pagedir, page->addr, true);
  return true;
}

/* Load a given PAGE with zeros. */
bool
page_load_zero (struct page *page)
{
  struct thread *t = thread_current ();
  void *kpage = frame_alloc (page->addr, PAL_ZERO);
  bool success;

  ASSERT (!page->loaded);

  if (kpage == NULL)
    return false;
  success = (pagedir_get_page (t->pagedir, page->addr) == NULL
             && pagedir_set_page (t->pagedir, page->addr, kpage, true));
  if (!success)
    {
      frame_free (kpage);
      return false;
    }
  pagedir_set_accessed (t->pagedir, page->addr, true);
  return true;
}

/* Returns a hash value for page P. */
static unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page A precedes page B. */
static bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->addr < b->addr;
}

/* Free a page. */
static void
page_destructor (struct hash_elem *e, void *aux UNUSED)
{
  struct thread *t = thread_current ();
  struct page *page;
  void *kpage;

  page = hash_entry (e, struct page, hash_elem);
  kpage = pagedir_get_page (t->pagedir, page->addr);
  if (kpage != NULL)
    {
      if (page->mapid != MAP_FAILED)
        {
          if (pagedir_is_dirty (t->pagedir, page->addr))
            file_write_at (page->file, page->addr, page->file_read_bytes,
                           page->file_ofs);
          list_remove (&page->elem);
        }
      pagedir_clear_page (t->pagedir, page->addr);
      frame_free (kpage);
    }
  if (!page->valid)
    swap_destroy (page->swap_idx);
  free (page);
}
