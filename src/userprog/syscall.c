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
static void sys_halt (void);
static void sys_exit (int status);
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

/* List of fds. */
struct desc_list
  {
    int tail;                   /* Current pointer. */
    int size;                   /* Size of the list. */
    struct file **list;         /* Container of file pointers. */
  };

/* List of fds in current process. */
static struct desc_list fd_list;

static struct file *desc_list_get (struct desc_list *list, int fd);
static void desc_list_free (struct desc_list *list, int fd);
static int desc_list_insert (struct desc_list *list, struct file *file);

void
syscall_init (void)
{
  int i;

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fd_list.tail = 0;
  fd_list.size = FD_LIST_SIZE;
  fd_list.list = (struct file **)
    malloc (fd_list.size * sizeof (struct file *));
  for (i = 0; i < fd_list.size; i++)
    *(fd_list.list + i) = NULL;
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
      sys_halt ();
      break;
    case SYS_EXIT:
      status = *(int *) arg1;
      f->eax = status;
      sys_exit (status);
      break;
    case SYS_EXEC:
      file = *(char **) arg1;
      f->eax = sys_exec (file);
      break;
    case SYS_WAIT:
      pid = *(pid_t *) arg1;
      f->eax = sys_wait (pid);
      break;
    case SYS_CREATE:
      file = *(char **) arg1;
      initial_size = *(unsigned *) arg2;
      f->eax = sys_create (file, initial_size);
      break;
    case SYS_REMOVE:
      file = *(char **) arg1;
      f->eax = sys_remove (file);
      break;
    case SYS_OPEN:
      file = *(char **) arg1;
      f->eax = sys_open (file);
      break;
    case SYS_FILESIZE:
      fd = *(int *) arg1;
      f->eax = sys_filesize (fd);
      break;
    case SYS_READ:
      fd = *(int *) arg1;
      rbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_read (fd, rbuffer, size);
      break;
    case SYS_WRITE:
      fd = *(int *) arg1;
      wbuffer = *(void **) arg2;
      size = *(unsigned *) arg3;
      f->eax = sys_write (fd, wbuffer, size);
      break;
    case SYS_SEEK:
      fd = *(int *) arg1;
      position = *(unsigned *) arg2;
      sys_seek (fd, position);
      break;
    case SYS_TELL:
      fd = *(int *) arg1;
      f->eax = sys_tell (fd);
      break;
    case SYS_CLOSE:
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

  free (fd_list.list);
  power_off ();
}

static void
sys_exit (int status)
{
  const char *name = thread_name ();
  size_t size = strlen (name) + 1;
  char *name_copy = (char *) malloc (size * sizeof (char));
  char *token, *save_ptr;

#if PRINT_DEBUG
  printf ("SYS_EXIT: status: %d\n", status);
#endif

  strlcpy (name_copy, name, size);
  token = strtok_r (name_copy, " ", &save_ptr);
  printf ("%s: exit(%d)\n", token, status);
  free (name_copy);
  free (fd_list.list);
  thread_exit ();
}

static pid_t
sys_exec (const char *file)
{
#if PRINT_DEBUG
  printf ("SYS_EXEC: file: %s\n", file);
#endif

  return process_execute (file);
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
#if PRINT_DEBUG
  printf ("SYS_CREATE: file: %s, initial_size: %u\n", file, initial_size);
#endif

  return filesys_create (file, initial_size);
}

static bool
sys_remove (const char *file)
{
#if PRINT_DEBUG
  printf ("SYS_REMOVE: file: %s\n", file);
#endif

  return filesys_remove (file);
}

static int
sys_open (const char *file)
{
  struct file *f;

#if PRINT_DEBUG
  printf ("SYS_OPEN: file: %s\n", file);
#endif

  f = filesys_open (file);
  if (f == NULL)
    return -1;
  return desc_list_insert (&fd_list, f);
}

static int
sys_filesize (int fd)
{
#if PRINT_DEBUG
  printf ("SYS_FILESIZE: fd: %d\n", fd);
#endif

  return file_length (desc_list_get (&fd_list, fd));
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  unsigned i;

#if PRINT_DEBUG
  printf ("SYS_READ: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

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
  else
    return file_read (desc_list_get (&fd_list, fd), buffer, size);
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
#if PRINT_DEBUG
  printf ("SYS_WRITE: fd: %d, buffer: %p, size: %u\n", fd, buffer, size);
#endif

  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    return file_write (desc_list_get (&fd_list, fd), buffer, size);
}

static void
sys_seek (int fd, unsigned position)
{
#if PRINT_DEBUG
  printf ("SYS_SEEK: fd: %d, position: %u\n", fd, position);
#endif

  file_seek (desc_list_get (&fd_list, fd), position);
}

static unsigned
sys_tell (int fd)
{
#if PRINT_DEBUG
  printf ("SYS_TELL: fd: %d\n", fd);
#endif

  return file_tell (desc_list_get (&fd_list, fd));
}

static void
sys_close (int fd)
{
#if PRINT_DEBUG
  printf ("SYS_CLOSE: fd: %d\n", fd);
#endif

  file_close (desc_list_get (&fd_list, fd));
  desc_list_free (&fd_list, fd);
}

/* Returns the file pointer with given FD. */
static struct file *
desc_list_get (struct desc_list *list, int fd)
{
  return *(list->list + fd - 2);
}

/* Set the file pointer at FD to NULL. */
static void
desc_list_free (struct desc_list *list, int fd)
{
  *(list->list + fd - 2) = NULL;
}

/* Add FILE to fd_list and return its fd. */
static int
desc_list_insert (struct desc_list *list, struct file *file)
{
  int fd;
  int tail, size;
  int i;

  *(list->list + list->tail) = file;
  fd = list->tail + 2;
  tail = list->tail;
  size = list->size;
  for (i = 0; i < size; i++)
    {
      tail = (tail + 1) % size;
      if (*(list->list + tail) == NULL)
        {
          list->tail = tail;
          return fd;
        }
    }
  list->tail = list->size;
  list->size <<= 1;
  list->list = (struct file **)
    realloc (&list->list, list->size * sizeof (struct file *));
  for (i = list->tail; i < list->size; i++)
    *(list->list + i) = NULL;
  return fd;
}
