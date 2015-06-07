#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "devices/disk.h"
#include "threads/synch.h"

struct cache
  {
    uint8_t buffer[DISK_SECTOR_SIZE];   /* Buffer. */
    disk_sector_t sec_no;               /* Sector number of disk. */
    bool loaded;                        /* Cache is loaded. */
    bool dirty;                         /* Dirty bit. */
    struct lock lock;                   /* Lock for writing. */
    struct list_elem elem;              /* List element. */
  };

void cache_init (void);
void cache_read (disk_sector_t, void *buffer, int sector_ofs, int size);
void cache_write (disk_sector_t, const void *buffer, int sector_ofs, int size);
void cache_request (disk_sector_t sec_no);
void cache_clear (void);

#endif /* filesys/cache.h */
