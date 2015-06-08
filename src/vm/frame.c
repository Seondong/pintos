#include "vm/frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <list.h>
#include <user/syscall.h>
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/swap.h"

/* Frame table. */
static struct list frame_table;
static struct lock frame_lock;

/* Initializes the frame table. */
void
frame_init (void)
{
  list_init (&frame_table);
  lock_init (&frame_lock);
}

/* Allocates a frame. */
void *
frame_alloc (void *upage, enum palloc_flags flags)
{
  struct frame *frame;
  void *page = palloc_get_page (PAL_USER | flags);

  if (page == NULL)
    page = frame_evict (flags);

  if (page != NULL)
    {
      frame = (struct frame *) malloc (sizeof (struct frame));
      frame->thread = thread_current ();
      frame->addr = page;
      frame->upage = upage;
      list_push_back (&frame_table, &frame->elem);
    }

  return page;
}

/* Frees a frame. */
void
frame_free (void *page)
{
  struct list_elem *e;
  struct frame *frame;
  for (e = list_begin (&frame_table); e != list_end (&frame_table);
       e = list_next (e))
    {
      frame = list_entry (e, struct frame, elem);
      if (frame->addr == page)
        {
          list_remove (e);
          palloc_free_page (frame->addr);
          free (frame);
          break;
        }
    }
}

/* Evicts a frame and return a address of new allocated frame. */
void *
frame_evict (enum palloc_flags flags)
{
  struct list_elem *e;
  struct frame *frame = NULL;
  struct page *page;

  /* Second chance algorithm. */
  e = list_begin (&frame_table);
  while (true)
    {
      frame = list_entry (e, struct frame, elem);
      if (pagedir_is_accessed (frame->thread->pagedir, frame->upage))
        pagedir_set_accessed (frame->thread->pagedir, frame->upage, false);
      else
        {
          page = page_find (&frame->thread->page_table, frame->upage);
          if (pagedir_is_dirty (frame->thread->pagedir, frame->upage))
            {
              if (page->mapid != MAP_FAILED)
                {
                  filesys_acquire ();
                  file_write_at (page->file, page->addr, page->file_read_bytes,
                                 page->file_ofs);
                  filesys_release ();
                  page->loaded = false;
                }
              else
                {
                  page->valid = false;
                  page->swap_idx = swap_out (frame->addr);
                }
            }
          else
            page->loaded = false;
          list_remove (e);
          pagedir_clear_page (frame->thread->pagedir, frame->upage);
          palloc_free_page (frame->addr);
          free (frame);

          return palloc_get_page (PAL_USER | flags);
        }

      e = list_next (e);
      if (e == list_end (&frame_table))
        e = list_begin (&frame_table);
    }
}

void
frame_acquire (void)
{
  lock_acquire (&frame_lock);
}

void
frame_release (void)
{
  lock_release (&frame_lock);
}
