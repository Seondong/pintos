#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"

void frame_init (void);
void *frame_alloc (enum palloc_flags);
void frame_free (void *page);

#endif /* vm/frame.h */
