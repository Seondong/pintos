#include "userprog/syscall.h"
#include <stdio.h>
#include <list.h>
#include <string.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "userprog/process.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

/* Debug flag. */
#ifndef PRINT_DEBUG
#define PRINT_DEBUG 0
#endif

static void syscall_handler (struct intr_frame *);
static void sys_halt (void);
static pid_t sys_exec (const char *file);
static int sys_wait (pid_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char *file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

static struct file *thread_fd_get (int fd);
static void thread_fd_free (int fd);
static int thread_fd_insert (struct file *file);

static struct lock filesys_lock;

static void filesys_acquire (void);
static void filesys_release (void);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f)
{
  int syscall_nr;
  void *arg1 = (int *) f->esp + 1;
  void *arg2 = (int *) f->esp + 2;
  void *arg3 = (int *) f->esp + 3;
  int status;
  const char *file;
  pid_t pid;
  unsigned initial_size, size, position;
  int fd;
  void *rbuffer;
  const void *wbuffer;

  if (!is_user_vaddr ((int *) f->esp))
    sys_exit (-1);
  syscall_nr = *(int *) f->esp;

  switch (syscall_nr)
    {
    case SYS_HALT:
      sys_halt ();
      break;
    case SYS_EXIT:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      status = *(int *) arg1;
      sys_exit (status);
      break;
    case SYS_EXEC:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      file = *(char **) arg1;
      f->eax = sys_exec (file);
      break;
    case SYS_WAIT:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      pid = *(pid_t *) arg1;
      f->eax = sys_wait (pid);
      break;
    case SYS_CREATE:
      if (!is_user_vaddr (arg2))
        sys_exit (-1);
      file = *(char **) arg1;
      initial_size = *(unsigned *) arg2;
      f->eax = sys_create (file, initial_size);
      break;
    case SYS_REMOVE:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      file = *(char **) arg1;
      f->eax = sys_remove (file);
      break;
    case SYS_OPEN:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      file = *(char **) arg1;
      f->eax = sys_open (file);
      break;
    case SYS_FILESIZE:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      fd = *(int *) arg1;
      f->eax = sys_filesize (fd);
      break;
    case SYS_READ:
      if (!is_user_vaddr (arg3))
        sys_exit (-1);
      fd = *(int *) arg1;
      rbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_read (fd, rbuffer, size);
      break;
    case SYS_WRITE:
      if (!is_user_vaddr (arg3))
        sys_exit (-1);
      fd = *(int *) arg1;
      wbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_write (fd, wbuffer, size);
      break;
    case SYS_SEEK:
      if (!is_user_vaddr (arg2))
        sys_exit (-1);
      fd = *(int *) arg1;
      position = *(unsigned *) arg2;
      sys_seek (fd, position);
      break;
    case SYS_TELL:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      fd = *(int *) arg1;
      f->eax = sys_tell (fd);
      break;
    case SYS_CLOSE:
      if (!is_user_vaddr (arg1))
        sys_exit (-1);
      fd = *(int *) arg1;
      sys_close (fd);
      break;
    }
}

static void
sys_halt (void)
{
#if PRINT_DEBUG
  printf ("SYS_HALT\n");
#endif

  power_off ();
}

void
sys_exit (int status)
{
  struct thread *curr = thread_current ();
  const char *name = thread_name ();
  size_t size = strlen (name) + 1;
  char *name_copy = (char *) malloc (size * sizeof (char));
  char *token, *save_ptr;
  struct thread_fd *tfd;
  struct list_elem *e;

#if PRINT_DEBUG
  printf ("SYS_EXIT: status: %d\n", status);
#endif

  strlcpy (name_copy, name, size);
  token = strtok_r (name_copy, " ", &save_ptr);
  printf ("%s: exit(%d)\n", token, status);
  free (name_copy);
  if (!list_empty (&curr->fd_list))
    {
      e = list_front (&curr->fd_list);
      while (e != list_end (&curr->fd_list))
        {
          tfd = list_entry (e, struct thread_fd, elem);
          e = list_remove (e);
          if (tfd->file != NULL)
            {
              filesys_acquire ();
              file_close (tfd->file);
              filesys_release ();
            }
          free (tfd);
        }
    }
  filesys_acquire ();
  file_close (curr->executable);
  filesys_release ();
  curr->exit_status = status;
  thread_exit ();
}

static pid_t
sys_exec (const char *file)
{
  pid_t pid;
  struct thread *curr = thread_current ();

#if PRINT_DEBUG
  printf ("SYS_EXEC: file: %s\n", file);
#endif

  if (!is_user_vaddr (file))
    sys_exit (-1);

  pid = process_execute (file);
  sema_down (&curr->load_sema);
  return curr->child_status == FAILED ? -1 : pid;
}

static int
sys_wait (pid_t pid)
{
#if PRINT_DEBUG
  printf ("SYS_WAIT: pid: %d\n", pid);
#endif

  return process_wait (pid);
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  bool success;

#if PRINT_DEBUG
  printf ("SYS_CREATE: file: %s, initial_size: %u\n", file, initial_size);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);

  filesys_acquire ();
  success = filesys_create (file, initial_size);
  filesys_release ();
  return success;
}

static bool
sys_remove (const char *file)
{
  bool success;

#if PRINT_DEBUG
  printf ("SYS_REMOVE: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);

  filesys_acquire ();
  success = filesys_remove (file);
  filesys_release ();
  return success;
}

static int
sys_open (const char *file)
{
  struct file *f;
  int fd;

#if PRINT_DEBUG
  printf ("SYS_OPEN: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (-1);

  filesys_acquire ();
  f = filesys_open (file);
  if (f == NULL)
    {
      filesys_release ();
      return -1;
    }
  fd = thread_fd_insert (f);
  filesys_release ();
  return fd;
}

static int
sys_filesize (int fd)
{
  struct file *file;
  int size;

#if PRINT_DEBUG
  printf ("SYS_FILESIZE: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  size = file_length (file);
  filesys_release ();
  return size;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  struct file *file;
  unsigned i;
  int bytes;

#if PRINT_DEBUG
  printf ("SYS_READ: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (!is_user_vaddr (buffer))
    sys_exit (-1);

  if (fd == 0)
    {
      for (i = 0; i < size; i++)
        {
          *((uint8_t *) buffer + i) = input_getc ();
          if (*((uint8_t *) buffer + i) == 0)
            break;
        }
      return i;
    }

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  bytes = file_read (file, buffer, size);
  filesys_release ();
  return bytes;
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
  struct file *file;
  int bytes;

#if PRINT_DEBUG
  printf ("SYS_WRITE: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (!is_user_vaddr (buffer))
    sys_exit (-1);

  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  bytes = file_write (file, buffer, size);
  filesys_release ();
  return bytes;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_SEEK: fd: %d, position: %u\n", fd, position);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  file_seek (file, position);
  filesys_release ();
}

static unsigned
sys_tell (int fd)
{
  struct file *file;
  unsigned offset;

#if PRINT_DEBUG
  printf ("SYS_TELL: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  offset = file_tell (file);
  filesys_release ();
  return offset;
}

static void
sys_close (int fd)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_CLOSE: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (-1);

  filesys_acquire ();
  file_close (file);
  thread_fd_free (fd);
  filesys_release ();
}

/* Returns the file pointer with given FD. */
static struct file *
thread_fd_get (int fd)
{
  struct thread *curr = thread_current ();
  struct thread_fd *tfd;
  struct list_elem *e;

  if (fd < 2 || fd >= curr->max_fd)
    return NULL;
  for (e = list_begin (&curr->fd_list); e != list_end (&curr->fd_list);
       e = list_next (e))
    {
      tfd = list_entry (e, struct thread_fd, elem);
      if (tfd->fd == fd)
        return tfd->file;
    }
  return NULL;
}

/* Set the file pointer at FD to NULL. */
static void
thread_fd_free (int fd)
{
  struct thread *curr = thread_current ();
  struct thread_fd *tfd;
  struct list_elem *e;

  for (e = list_begin (&curr->fd_list); e != list_end (&curr->fd_list);
       e = list_next (e))
    {
      tfd = list_entry (e, struct thread_fd, elem);
      if (tfd->fd == fd)
        {
          list_remove (e);
          free (tfd);
          break;
        }
    }
}

/* Add FILE to fd_list of current thread and return its fd. */
static int
thread_fd_insert (struct file *file)
{
  struct thread *curr = thread_current ();
  struct thread_fd *tfd = (struct thread_fd *)
    malloc (sizeof (struct thread_fd));

  tfd->fd = curr->max_fd++;
  tfd->file = file;
  list_push_back (&curr->fd_list, &tfd->elem);

  return tfd->fd;
}

/* Acquire the filesys_lock to usage file system. */
static void
filesys_acquire (void)
{
  if (!lock_held_by_current_thread (&filesys_lock))
    lock_acquire (&filesys_lock);
}

/* Release the filesys_lock. */
static void
filesys_release (void)
{
  if (lock_held_by_current_thread (&filesys_lock))
    lock_release (&filesys_lock);
}
