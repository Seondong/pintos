#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void sys_exit (int status);
void filesys_acquire (void);
void filesys_release (void);

#endif /* userprog/syscall.h */
