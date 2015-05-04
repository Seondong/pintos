#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/palloc.h"
#include "threads/thread.h"

/* Frame. */
struct frame
  {
    struct thread *thread;              /* Thread. */
    void *addr;                         /* Kernel virtual address. */
    void *upage;                        /* User virtual address. */
    struct list_elem elem;              /* List element. */
  };

void frame_init (void);
void *frame_alloc (void *upage, enum palloc_flags);
void frame_free (void *page);
void *frame_evict (enum palloc_flags);

#endif /* vm/frame.h */
