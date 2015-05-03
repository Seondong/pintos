#include "vm/swap.h"
#include <debug.h>
#include <stdbool.h>
#include <stdint.h>
#include <bitmap.h>
#include "devices/disk.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"

/* Swap table. */
static struct bitmap *swap_table = NULL;

/* Initializes the swap table. */
void
swap_init (void)
{
  struct disk *d = disk_get (1, 1);
  swap_table = bitmap_create (disk_size (d) * DISK_SECTOR_SIZE / PGSIZE);
  ASSERT (swap_table != NULL);
}

/* Swap out a frame. */
size_t
swap_out (void *kpage)
{
  struct disk *d = disk_get (1, 1);
  size_t swap_idx;
  disk_sector_t sec_no;

  swap_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (swap_idx == BITMAP_ERROR)
    PANIC ("swap_out: out of swap slots");
  for (sec_no = 0; sec_no < PGSIZE / DISK_SECTOR_SIZE; sec_no++)
    disk_write (d, swap_idx * PGSIZE / DISK_SECTOR_SIZE + sec_no,
                kpage + sec_no * DISK_SECTOR_SIZE);
  return swap_idx;
}

/* Swap the frame KPAGE in for the given PAGE. */
void
swap_in (struct page *page, void *kpage)
{
  struct disk *d = disk_get (1, 1);
  disk_sector_t sec_no;

  ASSERT (bitmap_test (swap_table, page->swap_idx));

  for (sec_no = 0; sec_no < PGSIZE / DISK_SECTOR_SIZE; sec_no++)
    disk_read (d, page->swap_idx * PGSIZE / DISK_SECTOR_SIZE + sec_no,
               kpage + sec_no * DISK_SECTOR_SIZE);
  bitmap_set (swap_table, page->swap_idx, false);
}

/* Destroys a swap. */
void
swap_destroy (size_t swap_idx)
{
  bitmap_set (swap_table, swap_idx, false);
}
