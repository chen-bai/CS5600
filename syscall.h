#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "vm/suppage.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

void syscall_init (void);
void close_all (void);
void munmap_all(void);
struct lock file_lock;
struct mmap_elem
{
   mapid_t mapid;
   struct file *file;
   bool writable;
   struct sup_page_table *spt;
   struct list_elem elem
};
#endif /* userprog/syscall.h */
