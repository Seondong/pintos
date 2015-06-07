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

static struct cache *cache_insert (disk_sector_t sec_no);
static struct cache *cache_find (disk_sector_t sec_no);
static void cache_flush (struct cache *cache);
static void cache_flush_all (void);
static void cache_evict (void);
static void cache_write_behind (void *aux UNUSED);
static void cache_acquire (void);
static void cache_release (void);

static struct list cache_list;
static struct list cache_free_list;
static struct lock cache_lock;

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

  tid = thread_create ("cache_write_behind", PRI_DEFAULT,
                       cache_write_behind, NULL);
  ASSERT (tid != TID_ERROR);
}

void
cache_read (disk_sector_t sec_no, void *buffer, int sector_ofs, int size)
{
  struct cache *cache;

  cache_acquire ();
  cache = cache_find (sec_no);
  if (cache == NULL)
    {
      cache = cache_insert (sec_no);
      lock_acquire (&cache->lock);
      disk_read (filesys_disk, sec_no, cache->buffer);
    }
  else
    lock_acquire (&cache->lock);
  memcpy ((uint8_t *) buffer, cache->buffer + sector_ofs, size);
  lock_release (&cache->lock);
  cache_release ();
}

void
cache_write (disk_sector_t sec_no, const void *buffer, int sector_ofs, int size)
{
  struct cache *cache;

  cache_acquire ();
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
  cache->dirty = true;
  memcpy (cache->buffer + sector_ofs, (const uint8_t *) buffer, size);
  lock_release (&cache->lock);
  cache_release ();
}

static void
cache_flush (struct cache *cache)
{
  lock_acquire (&cache->lock);
  if (cache->dirty)
    {
      disk_write (filesys_disk, cache->sec_no, cache->buffer);
      cache->dirty = false;
    }
  lock_release (&cache->lock);
}

static void
cache_flush_all (void)
{
  struct list_elem *e;
  struct cache *cache;

  cache_acquire ();
  for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      cache_flush (cache);
    }
  cache_release ();
}

void
cache_clear (void)
{
  struct cache *cache;

  cache_acquire ();
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
  cache_release ();
}

static struct cache *
cache_insert (disk_sector_t sec_no)
{
  struct cache *cache;

  if (list_empty (&cache_free_list))
    cache_evict ();
  cache = list_entry (list_pop_back (&cache_free_list), struct cache, elem);
  cache->sec_no = sec_no;
  cache->dirty = false;
  list_push_front (&cache_list, &cache->elem);
  return cache;
}

static struct cache *
cache_find (disk_sector_t sec_no)
{
  struct list_elem *e;
  struct cache *cache;

  for (e = list_begin (&cache_list); e != list_end (&cache_list);
       e = list_next (e))
    {
      cache = list_entry (e, struct cache, elem);
      if (cache->sec_no == sec_no)
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

static void
cache_evict (void)
{
  struct cache *cache;

  cache = list_entry (list_pop_back (&cache_list), struct cache, elem);
  cache_flush (cache);
  list_push_front (&cache_free_list, &cache->elem);
}

static void
cache_write_behind (void *aux UNUSED)
{
  while (true)
    {
      timer_sleep (CACHE_WRITE_BEHIND_INTERVAL);
      cache_flush_all ();
    }
}

static void
cache_acquire (void)
{
  if (!lock_held_by_current_thread (&cache_lock))
    lock_acquire (&cache_lock);
}

static void
cache_release (void)
{
  if (lock_held_by_current_thread (&cache_lock))
    lock_release (&cache_lock);
}
