#include "userprog/syscall.h"
#include <stdio.h>
#include <list.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include <debug.h>
#include <string.h>
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#define CODE_BASE ((void *) 0x08000000)
static void syscall_handler (struct intr_frame *);
void
check_valid_esp(const void *esp,struct intr_frame *f);
void halt(void);
void exit(int status);
pid_t exec(const char*cmd_line);
int wait(pid_t pid);
bool remove (const char *file);
bool create (const char *file, unsigned initial_size);
int filesize (int fd);
int read (int fd, void *buffer, unsigned length);
int write (int fd, const void *buffer, unsigned length);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
void close_all (void);
struct file *find_file(struct child_process_elem *process,int fd);
struct file_elem
{
   struct file *file;
   int fd;
   struct list_elem elem;
};
int open (const char *file);
void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  check_valid_esp((void *)f->esp,f);
  switch (* (int *) f->esp)
  {
     case SYS_HALT:
     {
        halt();
        break;
     }
     case SYS_EXIT:
     {
        check_valid_esp((void *)(f->esp+4),f);
        exit(*(int *) (f->esp + 4));
        break;
     }
     case SYS_EXEC:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=exec(*(char **) (f->esp + 4));
        break;
     }
     case SYS_WAIT:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=wait(*(pid_t *) (f->esp + 4));
        break;
     }
     case SYS_CREATE:
     {
        check_valid_esp((void *)(f->esp+4),f);
        check_valid_esp((void *)(f->esp+8),f);
        f->eax=create(*(const char **)(f->esp + 4),*(unsigned *)(f->esp + 8));
        break;
     }
     case SYS_REMOVE:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=remove(*(const char **)(f->esp + 4));
        break;
     }
     case SYS_OPEN:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=open(*(const char **)(f->esp + 4));
        break;
     }
     case SYS_FILESIZE:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=filesize(*(int *)(f->esp + 4));
        break;
     }
     case SYS_READ:
     {
        check_valid_esp((void *)(f->esp+4),f);
        check_valid_esp((void *)(f->esp+8),f);
        check_valid_esp((void *)(f->esp+12),f);
        unsigned size=*(unsigned *)(f->esp+12);
        void *buffer=*(void **)(f->esp+8);
        int fd=*(int *)(f->esp+4);
        if(fd==0)
        {
           uint8_t *buffer_cpy=(uint8_t *)buffer;
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy,f);
              buffer_cpy++;
           }
        }
        else
        {
           char *buffer_cpy=(char *)buffer;
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy,f);
              buffer_cpy++;
           }
        }
        f->eax=read (fd,buffer,size);
        break;
     }
     case SYS_WRITE:
     {
   	check_valid_esp((void *)(f->esp+4),f);
        check_valid_esp((void *)(f->esp+8),f);
        check_valid_esp((void *)(f->esp+12),f);
        unsigned size=*(unsigned *)(f->esp+12);
        void *buffer=*(void **)(f->esp+8);
        char *buffer_cpy=(char *)buffer;
        unsigned i;
        for(i=0;i<size;i++)
        {
           check_valid_esp(buffer_cpy,f);
           buffer_cpy++;
        }
        f->eax=write (*(int *)(f->esp+4),buffer,size);
        break;
     }
     case SYS_SEEK:
     {
	check_valid_esp((void *)(f->esp+4),f);
        check_valid_esp((void *)(f->esp+8),f);
        seek(*(int *)(f->esp+4),*(unsigned *)(f->esp+8));
        break;
     }
     case SYS_TELL:
     {
        check_valid_esp((void *)(f->esp+4),f);
        f->eax=tell(*(int *)(f->esp+4));
	break;
     }
     case SYS_CLOSE:
     {
        check_valid_esp((void *)(f->esp+4),f);
        close(*(int *)(f->esp+4));
	break;
     }
   }
}
void
check_valid_esp(const void *esp,struct intr_frame *f)
{
   if(esp < PHYS_BASE && esp >= CODE_BASE)
   {
      page_fault(f);
      thread_exit();
   }
}

void halt(void)
{
   shutdown_power_off();
}

void exit(int status)
{
   struct thread *cur=thread_current();
   cur->process->status=status;
   cur->process->exit=true;
   close_all();
   thread_exit();
}

pid_t exec(const char*cmd_line)
{
   pid_t pid=process_execute(cmd_line);
   return pid;
}

int wait(pid_t pid)
{
   return process_wait(pid);
}
bool create (const char *file, unsigned initial_size)
{
   bool success = filesys_create (file, initial_size);
   return success;   
}
bool remove (const char *file)
{
   bool success = filesys_remove (file);
   return success;
}
int open (const char *file)
{
   struct file *file_ret=filesys_open (file);
   if(file_ret==NULL)
      return -1;
   struct file_elem *fe;
   list_push_back(&thread_current()->process->file_list,&fe->elem);
   fe->fd=thread_current()->process->next_fd++;
   fe->file=file_ret;
   return fe->fd;
}
struct file *find_file(struct child_process_elem *process,int fd)
{
   struct list_elem *e;
   for(e=list_begin(&process->file_list);e!=list_end(&process->file_list);e=list_next(e))
   {
      if(list_entry(e,struct file_elem,elem)->fd==fd)
      {
         struct file *f=list_entry(e,struct file_elem,elem)->file;
         return f;
      }
   }
   return NULL;
}
int filesize (int fd)
{
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
      return -1;
   return file_length(file);
}

int read (int fd, void *buffer, unsigned length)
{
   unsigned size=length;
   if(fd==0)
   {
      unsigned i;
      uint8_t *buffer_cpy=(uint8_t *)buffer;
      for(i=0;i<size;i++)
      {
         buffer_cpy=input_getc();
         buffer_cpy++;
      }
      return size;
   }
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
      return -1;
   return file_read(file, buffer, size);
}
int write (int fd, const void *buffer, unsigned length)
{
   unsigned size=length;
   if(fd==1)
   {
      putbuf(buffer, size);
      return size;
   }
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
      return -1;
   return file_write(file, buffer, size);
}
void seek (int fd, unsigned position)
{
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
      return;
   file_seek(file, position);
   return;
}
unsigned tell (int fd)
{
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
      return -1;
   return file_tell(file);
}
void close (int fd)
{
   struct thread *cur=thread_current();
   struct list_elem *e;
   for(e=list_begin(&cur->process->file_list);e!=list_end(&cur->process->file_list);e=list_next(e))
   {
      if(list_entry(e,struct file_elem,elem)->fd==fd)
      {
	  file_close(list_entry(e,struct file_elem,elem)->file);
	  list_remove(e);
          break;
      }
   }
   return;
}
void close_all(void)
{
   struct thread *cur=thread_current();
   struct list_elem *e;
   for(e=list_begin(&cur->process->file_list);e!=list_end(&cur->process->file_list);e=list_next(e))
   {
      file_close(list_entry(e,struct file_elem,elem)->file);
      list_remove(e);
   }
   return;
}
