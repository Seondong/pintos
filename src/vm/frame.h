#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"

/* Frame. */
struct frame
  {
    void *addr;                         /* Kernel virtual address. */
    struct list_elem elem;              /* List element. */
  };

void frame_init (void);
void *frame_alloc (enum palloc_flags);
void frame_free (void *page);

#endif /* vm/frame.h */
