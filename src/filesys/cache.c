#include "filesys/cache.h"
#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <list.h>
#include "devices/disk.h"
#include "devices/timer.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

#define CACHE_SIZE 64
#define CACHE_WRITE_BEHIND_INTERVAL 50

struct read_ahead_entry
  {
    disk_sector_t sec_no;               /* Sector number of disk. */
    struct list_elem elem;              /* List element. */
  };

static struct cache *cache_insert (disk_sector_t sec_no);
static struct cache *cache_find (disk_sector_t sec_no);
static void cache_flush (struct cache *cache);
static void cache_flush_all (void);
static void cache_evict (void);
static void cache_write_behind (void *aux UNUSED);
static void cache_read_ahead (void *aux UNUSED);

static struct list cache_list;
static struct list cache_free_list;
static struct lock cache_lock;
static struct list read_ahead_list;
static struct lock read_ahead_lock;
static struct condition read_ahead_cond;

/* Initializes the buffer cache. */
void
cache_init (void)
{
  struct cache *cache;
  int i;
  tid_t tid;

  list_init (&cache_list);
  list_init (&cache_free_list);
  for (i = 0; i < CACHE_SIZE; i++)
    {
      cache = (struct cache *) malloc (sizeof (struct cache));
      lock_init (&cache->lock);
      list_push_front (&cache_free_list, &cache->elem);
    }
  lock_init (&cache_lock);
  list_init (&read_ahead_list);
  lock_init (&read_ahead_lock);
  cond_init (&read_ahead_cond);

  tid = thread_create ("cache_write_behind", PRI_DEFAULT,
                       cache_write_behind, NULL);
  ASSERT (tid != TID_ERROR);

  tid = thread_create ("cache_read_ahead", PRI_DEFAULT, cache_read_ahead, NULL);
  ASSERT (tid != TID_ERROR);
}

/* Read SIZE bytes from SEC_NO sector into BUFFER using buffer
   cache. */
void
cache_read (disk_sector_t sec_no, void *buffer, int sector_ofs, int size)
{
  struct cache *cache;

  lock_acquire (&cache_lock);
  cache = cache_find (sec_no);
  if (cache == NULL)
    {
      cache = cache_insert (sec_no);
      lock_acquire (&cache->lock);
      disk_read (filesys_disk, sec_no, cache->buffer);
      cache->loaded = true;
    }
  else
    lock_acquire (&cache->lock);
  memcpy ((uint8_t *) buffer, cache->buffer + sector_ofs, size);
  lock_release (&cache->lock);
  lock_release (&cache_lock);
}

/* Write SIZE bytes from BUFFER into SEC_NO sector using buffer
   cache. */
void
cache_write (disk_sector_t sec_no, const void *buffer, int sector_ofs, int size)
{
  struct cache *cache;

  lock_acquire (&cache_lock);
  cache = cache_find (sec_no);
  if (cache == NULL)
    {
      cache = cache_insert (sec_no);
      lock_acquire (&cache->lock);
      if (sector_ofs > 0 || size < DISK_SECTOR_SIZE)
        disk_read (filesys_disk, sec_no, cache->buffer);
    }
  else
    lock_acquire (&cache->lock);
  cache->loaded = true;
  cache->dirty = true;
  memcpy (cache->buffer + sector_ofs, (const uint8_t *) buffer, size);
  lock_release (&cache->lock);
  lock_release (&cache_lock);
}

/* Request a read-ahead SEC_NO sector into buffer cache. */
void
cache_request (disk_sector_t sec_no)
{
  struct read_ahead_entry *rae;

  lock_acquire (&cache_lock);
  if (cache_find (sec_no) != NULL)
    {
      lock_release (&cache_lock);
      return;
    }
  lock_release (&cache_lock);

  rae = (struct read_ahead_entry *) malloc (sizeof (struct read_ahead_entry));
  rae->sec_no = sec_no;
  lock_acquire (&read_ahead_lock);
  list_push_back (&read_ahead_list, &rae->elem);
  cond_signal (&read_ahead_cond, &read_ahead_lock);
  lock_release (&read_ahead_lock);
}

/* Flush modified CACHE into disk. */
static void
cache_flush (struct cache *cache)
{
  lock_acquire (&cache->lock);
  if (cache->loaded && cache->dirty)
    {
      disk_write (filesys_disk, cache->sec_no, cache->buffer);
      cache->dirty = false;
    }
  lock_release (&cache->lock);
}

/* Flush all modified cache into disk. */
static void
cache_flush_all (void)
{
  struct list_elem *e;
  struct cache *cache;

  lock_acquire (&cache_lock);
  for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      cache_flush (cache);
    }
  lock_release (&cache_lock);
}

/* Destroy buffer cache. */
void
cache_clear (void)
{
  struct cache *cache;

  lock_acquire (&cache_lock);
  while (!list_empty (&cache_list))
    {
      cache = list_entry (list_pop_back (&cache_list), struct cache, elem);
      cache_flush (cache);
      free (cache);
    }
  while (!list_empty (&cache_free_list))
    {
      cache = list_entry (list_pop_back (&cache_free_list), struct cache, elem);
      free (cache);
    }
  lock_release (&cache_lock);
}

/* Make a cache to store SEC_NO disk sector. */
static struct cache *
cache_insert (disk_sector_t sec_no)
{
  struct cache *cache;

  if (list_empty (&cache_free_list))
    cache_evict ();
  cache = list_entry (list_pop_back (&cache_free_list), struct cache, elem);
  cache->sec_no = sec_no;
  cache->loaded = false;
  cache->dirty = false;
  list_push_front (&cache_list, &cache->elem);
  return cache;
}

/* Find a cache holding SEC_NO disk sector. */
static struct cache *
cache_find (disk_sector_t sec_no)
{
  struct list_elem *e;
  struct cache *cache;

  for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      if (cache->loaded && cache->sec_no == sec_no)
        {
          lock_acquire (&cache->lock);
          list_remove (e);
          list_push_front (&cache_list, e);
          lock_release (&cache->lock);
          return cache;
        }
    }
  return NULL;
}

/* Flush the oldest cache and remove it. */
static void
cache_evict (void)
{
  struct cache *cache;

  cache = list_entry (list_pop_back (&cache_list), struct cache, elem);
  cache_flush (cache);
  list_push_front (&cache_free_list, &cache->elem);
}

/* Write-behind thread for buffer cache. */
static void
cache_write_behind (void *aux UNUSED)
{
  while (true)
    {
      timer_sleep (CACHE_WRITE_BEHIND_INTERVAL);
      cache_flush_all ();
    }
}

/* Read-ahead thread for buffer cache. */
static void
cache_read_ahead (void *aux UNUSED)
{
  struct read_ahead_entry *rae;
  struct cache *cache;

  while (true)
    {
      lock_acquire (&read_ahead_lock);
      while (list_empty (&read_ahead_list))
        cond_wait (&read_ahead_cond, &read_ahead_lock);
      rae = list_entry (list_pop_front (&read_ahead_list),
                        struct read_ahead_entry, elem);
      lock_release (&read_ahead_lock);

      lock_acquire (&cache_lock);
      cache = cache_find (rae->sec_no);
      if (cache == NULL)
        {
          cache = cache_insert (rae->sec_no);
          lock_acquire (&cache->lock);
          disk_read (filesys_disk, cache->sec_no, cache->buffer);
          cache->loaded = true;
          lock_release (&cache->lock);
        }
      lock_release (&cache_lock);
      free (rae);
    }
}
