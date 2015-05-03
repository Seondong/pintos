#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>
#include "vm/page.h"

void swap_init (void);
size_t swap_out (void *kpage);
void swap_in (struct page *page, void *kpage);
void swap_destroy (size_t swap_idx);

#endif /* vm/swap.h */
