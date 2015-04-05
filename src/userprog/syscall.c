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

/* Default value of the fd list. */
#define FD_LIST_SIZE 128

static void syscall_handler (struct intr_frame *);
static void sys_halt (int *eax);
static pid_t sys_exec (int *eax, const char *file);
static int sys_wait (int *eax, pid_t pid);
static bool sys_create (int *eax, const char *file, unsigned initial_size);
static bool sys_remove (int *eax, const char *file);
static int sys_open (int *eax, const char *file);
static int sys_filesize (int *eax, int fd);
static int sys_read (int *eax, int fd, void *buffer, unsigned size);
static int sys_write (int *eax, int fd, const void *buffer, unsigned size);
static void sys_seek (int *eax, int fd, unsigned position);
static unsigned sys_tell (int *eax, int fd);
static void sys_close (int *eax, int fd);

static struct file *thread_fd_get (int fd);
static void thread_fd_free (int fd);
static int thread_fd_insert (struct file *file);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  int syscall_nr = *(int *) f->esp;
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

  switch (syscall_nr)
    {
    case SYS_HALT:
      sys_halt (&f->eax);
      break;
    case SYS_EXIT:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      status = *(int *) arg1;
      sys_exit (&f->eax, status);
      break;
    case SYS_EXEC:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      file = *(char **) arg1;
      f->eax = sys_exec (&f->eax, file);
      break;
    case SYS_WAIT:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      pid = *(pid_t *) arg1;
      f->eax = sys_wait (&f->eax, pid);
      break;
    case SYS_CREATE:
      if (!is_user_vaddr (arg2))
        sys_exit (&f->eax, -1);
      file = *(char **) arg1;
      initial_size = *(unsigned *) arg2;
      f->eax = sys_create (&f->eax, file, initial_size);
      break;
    case SYS_REMOVE:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      file = *(char **) arg1;
      f->eax = sys_remove (&f->eax, file);
      break;
    case SYS_OPEN:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      file = *(char **) arg1;
      f->eax = sys_open (&f->eax, file);
      break;
    case SYS_FILESIZE:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      f->eax = sys_filesize (&f->eax, fd);
      break;
    case SYS_READ:
      if (!is_user_vaddr (arg3))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      rbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_read (&f->eax, fd, rbuffer, size);
      break;
    case SYS_WRITE:
      if (!is_user_vaddr (arg3))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      wbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_write (&f->eax, fd, wbuffer, size);
      break;
    case SYS_SEEK:
      if (!is_user_vaddr (arg2))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      position = *(unsigned *) arg2;
      sys_seek (&f->eax, fd, position);
      break;
    case SYS_TELL:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      f->eax = sys_tell (&f->eax, fd);
      break;
    case SYS_CLOSE:
      if (!is_user_vaddr (arg1))
        sys_exit (&f->eax, -1);
      fd = *(int *) arg1;
      sys_close (&f->eax, fd);
      break;
    }
}

static void
sys_halt (int *eax UNUSED)
{
#if PRINT_DEBUG
  printf ("SYS_HALT\n");
#endif

  power_off ();
}

void
sys_exit (int *eax, int status)
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
            file_close (tfd->file);
          free (tfd);
        }
    }
  curr->exit_status = status;
  *eax = status;
  thread_exit ();
}

static pid_t
sys_exec (int *eax, const char *file)
{
#if PRINT_DEBUG
  printf ("SYS_EXEC: file: %s\n", file);
#endif

  if (!is_user_vaddr (file))
    sys_exit (eax, -1);

  return process_execute (file);
}

static int
sys_wait (int *eax UNUSED, pid_t pid)
{
#if PRINT_DEBUG
  printf ("SYS_WAIT: pid: %d\n", pid);
#endif

  return process_wait (pid);
}

static bool
sys_create (int *eax, const char *file, unsigned initial_size)
{
#if PRINT_DEBUG
  printf ("SYS_CREATE: file: %s, initial_size: %u\n", file, initial_size);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (eax, -1);

  return filesys_create (file, initial_size);
}

static bool
sys_remove (int *eax, const char *file)
{
#if PRINT_DEBUG
  printf ("SYS_REMOVE: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (eax, -1);

  return filesys_remove (file);
}

static int
sys_open (int *eax, const char *file)
{
  struct file *f;

#if PRINT_DEBUG
  printf ("SYS_OPEN: file: %s\n", file);
#endif

  if (file == NULL || !is_user_vaddr (file))
    sys_exit (eax, -1);

  f = filesys_open (file);
  if (f == NULL)
    return -1;
  return thread_fd_insert (f);
}

static int
sys_filesize (int *eax, int fd)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_FILESIZE: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (eax, -1);

  return file_length (file);
}

static int
sys_read (int *eax, int fd, void *buffer, unsigned size)
{
  struct file *file;
  unsigned i;

#if PRINT_DEBUG
  printf ("SYS_READ: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (!is_user_vaddr (buffer))
    sys_exit (eax, -1);

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
    sys_exit (eax, -1);

  return file_read (file, buffer, size);
}

static int
sys_write (int *eax, int fd, const void *buffer, unsigned size)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_WRITE: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (!is_user_vaddr (buffer))
    sys_exit (eax, -1);

  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (eax, -1);

  return file_write (file, buffer, size);
}

static void
sys_seek (int *eax, int fd, unsigned position)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_SEEK: fd: %d, position: %u\n", fd, position);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (eax, -1);

  file_seek (file, position);
}

static unsigned
sys_tell (int *eax, int fd)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_TELL: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (eax, -1);

  return file_tell (file);
}

static void
sys_close (int *eax, int fd)
{
  struct file *file;

#if PRINT_DEBUG
  printf ("SYS_CLOSE: fd: %d\n", fd);
#endif

  file = thread_fd_get (fd);
  if (file == NULL)
    sys_exit (eax, -1);

  file_close (file);
  thread_fd_free (fd);
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
