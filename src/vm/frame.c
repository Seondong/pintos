#include "vm/frame.h"
#include <stddef.h>
#include <list.h>
#include "threads/malloc.h"

/* Frame. */
struct frame
  {
    void *addr;                         /* Physical address. */
    struct list_elem elem;              /* List element. */
  };

static struct list frame_table;

/* Initializes the frame table. */
void
frame_init (void)
{
  list_init (&frame_table);
}

/* Allocates a frame. */
void *
frame_alloc (enum palloc_flags flags)
{
  struct frame *frame;
  void *page = palloc_get_page (PAL_USER | flags);

  if (page != NULL)
    {
      frame = (struct frame *) malloc (sizeof (struct frame));
      frame->addr = page;
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
          list_remove(e);
          palloc_free_page (frame->addr);
          break;
        }
    }
}
