#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/directory.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INODE_DIRECT_BLOCKS 12
#define INODE_INDIRECT_BLOCKS 128
#define INODE_DOUBLE_INDIRECT_BLOCKS 128 * 128
#define INODE_OFFSET_LENGTH 0
#define INODE_OFFSET_IS_DIR 8
#define INODE_OFFSET_PARENT 12

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    size_t sector_count;                /* Number of used disk sectors. */
    bool is_dir;                        /* This is directory or not. */
    disk_sector_t parent;               /* Sector number of parent directory. */
    disk_sector_t directs[INODE_DIRECT_BLOCKS];     /* Direct blocks. */
    disk_sector_t indirect;             /* Single indirect block. */
    disk_sector_t double_indirect;      /* Double indirect block. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[109];               /* Not used. */
  };

/* Indirect block.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct indirect_block
  {
    disk_sector_t blocks[INODE_INDIRECT_BLOCKS];    /* Blocks. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  disk_sector_t sec_no = -1;
  disk_sector_t indirect_sector;
  struct inode_disk *disk;
  off_t offset;
  off_t indirect_offset;
  off_t double_indirect_offset;
  off_t entry_count;

  ASSERT (inode != NULL);

  if (pos >= inode_length (inode))
    return -1;

  offset = pos / DISK_SECTOR_SIZE;
  disk = (struct inode_disk *) malloc (sizeof *disk);
  ASSERT (disk != NULL);
  cache_read (inode->sector, disk, 0, sizeof (struct inode_disk));

  /* Read from direct block. */
  if (offset < INODE_DIRECT_BLOCKS)
    sec_no = disk->directs[offset];
  /* Read from indirect block. */
  else if (offset < INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS)
    {
      indirect_offset = offset - INODE_DIRECT_BLOCKS;
      cache_read (disk->indirect, (void *) &sec_no,
                  indirect_offset * sizeof (disk_sector_t),
                  sizeof (disk_sector_t));
    }
  /* Read from double indirect block. */
  else if (offset < (INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS
                     + INODE_DOUBLE_INDIRECT_BLOCKS))
    {
      entry_count = offset - (INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS);
      double_indirect_offset = entry_count / INODE_INDIRECT_BLOCKS;
      indirect_offset = entry_count % INODE_INDIRECT_BLOCKS;

      cache_read (disk->double_indirect, (void *) &indirect_sector,
                  double_indirect_offset * sizeof (disk_sector_t),
                  sizeof (disk_sector_t));

      cache_read (indirect_sector, (void *) &sec_no,
                  indirect_offset * sizeof (disk_sector_t),
                  sizeof (disk_sector_t));
    }
  free (disk);

  return sec_no;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Adds a sector SECTOR to INODE. */
static bool
inode_append (struct inode *inode, disk_sector_t sector)
{
  struct inode_disk *disk;
  off_t offset;
  off_t indirect_offset;
  off_t double_indirect_offset;
  off_t entry_count;
  disk_sector_t indirect_sector;

  disk = (struct inode_disk *) malloc (sizeof *disk);

  ASSERT (disk != NULL);

  cache_read (inode->sector, disk, 0, sizeof (struct inode_disk));

  /* Direct block. */
  if (disk->sector_count < INODE_DIRECT_BLOCKS)
    disk->directs[disk->sector_count] = sector;
  /* Indirect block. */
  else if (disk->sector_count < INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS)
    {
      offset = disk->sector_count - INODE_DIRECT_BLOCKS;

      /* Create an indirect block. */
      if (offset == 0)
        {
          static char zeros[DISK_SECTOR_SIZE];

          if (!free_map_allocate (1, &disk->indirect))
            return false;
          cache_write (disk->indirect, zeros, 0, DISK_SECTOR_SIZE);
        }

      cache_write (disk->indirect, (void *) &sector,
                   offset * sizeof (disk_sector_t), sizeof (disk_sector_t));
    }
  /* Double indirect block. */
  else if (disk->sector_count < (INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS
                                 + INODE_DOUBLE_INDIRECT_BLOCKS))
    {
      entry_count = disk->sector_count - (INODE_DIRECT_BLOCKS
                                          + INODE_INDIRECT_BLOCKS);
      double_indirect_offset = entry_count / INODE_INDIRECT_BLOCKS;
      indirect_offset = entry_count % INODE_INDIRECT_BLOCKS;

      /* Create a double indirect block. */
      if (entry_count == 0)
        {
          static char zeros[DISK_SECTOR_SIZE];

          if (!free_map_allocate (1, &disk->double_indirect))
            return false;
          cache_write (disk->double_indirect, zeros, 0, DISK_SECTOR_SIZE);
        }

      /* Create an indirect block. */
      if (indirect_offset == 0)
        {
          static char zeros[DISK_SECTOR_SIZE];

          if (!free_map_allocate (1, &indirect_sector))
            return false;
          cache_write (indirect_sector, zeros, 0, DISK_SECTOR_SIZE);

          cache_write (disk->double_indirect, (void *) &indirect_sector,
                       double_indirect_offset * sizeof (disk_sector_t),
                       sizeof (disk_sector_t));
        }

      cache_read (disk->double_indirect, (void *) &indirect_sector,
                  double_indirect_offset * sizeof (disk_sector_t),
                  sizeof (disk_sector_t));

      cache_write (indirect_sector, (void *) &sector,
                   indirect_offset * sizeof (disk_sector_t),
                   sizeof (disk_sector_t));
    }
  disk->sector_count++;
  cache_write (inode->sector, (void *) disk, 0, DISK_SECTOR_SIZE);
  free (disk);

  return true;
}

/* Extends the INODE with LENGTH bytes. */
static bool
inode_extend (struct inode *inode, off_t length)
{
  struct inode_disk *disk;
  off_t free_length;
  size_t sectors;
  disk_sector_t sector;
  size_t i;

  lock_acquire (&inode->lock);
  disk = (struct inode_disk *) malloc (sizeof *disk);

  ASSERT (disk != NULL);

  cache_read (inode->sector, disk, 0, DISK_SECTOR_SIZE);

  free_length = disk->sector_count * DISK_SECTOR_SIZE - disk->length;
  sectors = bytes_to_sectors (length - free_length);

  for (i = 0; i < sectors; i++)
    {
      if (!free_map_allocate (1, &sector)
          || !inode_append (inode, sector))
        {
          lock_release (&inode->lock);
          return false;
        }
    }
  disk->length += length;
  cache_write (inode->sector, &disk->length, INODE_OFFSET_LENGTH, 4);
  lock_release (&inode->lock);

  free (disk);
  return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  struct inode *inode;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = 0;
      disk_inode->sector_count = 0;
      disk_inode->is_dir = is_dir;
      disk_inode->parent = dir_get_inode (thread_current ()->dir)->sector;
      disk_inode->magic = INODE_MAGIC;

      cache_write (sector, disk_inode, 0, DISK_SECTOR_SIZE);
      inode = inode_open (sector);
      success = inode_extend (inode, length);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Returns whether INODE is directory or not. */
bool
inode_is_dir (const struct inode *inode)
{
  bool is_dir;
  cache_read (inode->sector, &is_dir, INODE_OFFSET_IS_DIR, sizeof (bool));

  return is_dir;
}

disk_sector_t
inode_get_parent (const struct inode *inode)
{
  disk_sector_t parent;
  cache_read (inode->sector, &parent, INODE_OFFSET_PARENT,
              sizeof (disk_sector_t));
  return parent;
}

/* Clear the INODE data. */
static void
inode_clear (struct inode *inode)
{
  struct inode_disk *disk;
  struct indirect_block *double_indirect;
  struct indirect_block *indirect;
  size_t double_entry_count;
  size_t entry_count;
  off_t offset;
  size_t i;

  disk = (struct inode_disk *) malloc (sizeof *disk);
  ASSERT (disk != NULL);
  cache_read (inode->sector, disk, 0, DISK_SECTOR_SIZE);

  ASSERT (disk->sector_count <= (INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS
                                 + INODE_DOUBLE_INDIRECT_BLOCKS));

  if (disk->sector_count == 0)
    return;

  /* Double indirect block. */
  if (disk->sector_count > INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS)
    {
      double_entry_count = disk->sector_count - (INODE_DIRECT_BLOCKS
                                                 + INODE_INDIRECT_BLOCKS);

      double_indirect = ((struct indirect_block *)
                         malloc (sizeof *double_indirect));
      ASSERT (double_indirect != NULL);
      cache_read (disk->double_indirect, double_indirect, 0, DISK_SECTOR_SIZE);

      indirect = (struct indirect_block *) malloc (sizeof *indirect);
      ASSERT (indirect != NULL);

      while (double_entry_count > 0)
        {
          offset = double_entry_count / INODE_INDIRECT_BLOCKS;
          cache_read (double_indirect->blocks[offset], indirect, 0,
                      DISK_SECTOR_SIZE);

          if (double_entry_count > INODE_INDIRECT_BLOCKS)
            entry_count = double_entry_count % INODE_INDIRECT_BLOCKS;
          else
            entry_count = double_entry_count;

          for (i = 0; i < entry_count; i++)
            free_map_release (indirect->blocks[i], 1);

          double_entry_count -= entry_count;
        }
      free (double_indirect);
      free (indirect);
      disk->sector_count = INODE_DIRECT_BLOCKS + INODE_INDIRECT_BLOCKS;
    }

  /* Indirect block. */
  if (disk->sector_count > INODE_DIRECT_BLOCKS)
    {
      entry_count = disk->sector_count - INODE_DIRECT_BLOCKS;

      indirect = (struct indirect_block *) malloc (sizeof *indirect);
      ASSERT (indirect != NULL);
      cache_read (disk->indirect, &indirect, 0, DISK_SECTOR_SIZE);

      for (i = 0; i < entry_count; i++)
        free_map_release (indirect->blocks[i], 1);

      free (indirect);
      disk->sector_count = INODE_DIRECT_BLOCKS;
    }

  /* Direct block. */
  for (i = 0; i < disk->sector_count; i++)
    free_map_release (disk->directs[i], 1);

  disk->length = 0;
  disk->sector_count = 0;
  cache_write (inode->sector, (void *) disk, 0, DISK_SECTOR_SIZE);
  free (disk);
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      lock_acquire (&inode->lock);

      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          inode_clear (inode);
          free_map_release (inode->sector, 1);
        }

      lock_release (&inode->lock);
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read sector through buffer cache. */
      cache_read (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      cache_request (sector_idx + 1);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if an error occurs. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  off_t length;

  if (inode->deny_write_cnt)
    return 0;

  /* Extend file. */
  length = inode_length (inode);
  if (offset + size > length)
    inode_extend (inode, offset + size - length);

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Write sector through buffer cache. */
      cache_write (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;

  cache_read (inode->sector, (void *) &length, INODE_OFFSET_LENGTH,
              sizeof (off_t));
  return length;
}
