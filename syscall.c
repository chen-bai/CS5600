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
#define CODE_BASE ((void *) 0x08048000)

static void syscall_handler (struct intr_frame *);
void
check_valid_esp(const void *esp);
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
  //printf("int:%s\n",thread_current()->name);
  check_valid_esp((const void *)f->esp);
  switch (* (int *) f->esp)
  {
     case SYS_HALT:
     {
        //printf("halt\n");
        halt();
        break;
     }
     case SYS_EXIT:
     {
        //printf("exit\n");
        check_valid_esp((void *)(f->esp+4));
        exit(*(int *) (f->esp + 4));
        break;
     }
     case SYS_EXEC:
     {
        //printf("exec\n");
        check_valid_esp((void *)(f->esp+4));
        check_valid_esp(*(void **)(f->esp+4));
        const char *cmd_line=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+4));
        f->eax=exec(cmd_line);
        //printf("<pid>:%d\n",f->eax);
        break;
     }
     case SYS_WAIT:
     {
        //printf("%s:wait\n",thread_current()->name);
        check_valid_esp((void *)(f->esp+4));
        //printf("wait:%d\n",*(pid_t *) (f->esp + 4));
        f->eax=wait(*(pid_t *) (f->esp + 4));
        break;
     }
     case SYS_CREATE:
     {
        //printf("create\n");
        check_valid_esp((void *)(f->esp+4));
        check_valid_esp((void *)(f->esp+8));
        check_valid_esp(*(void **)(f->esp+4));
        const char *file_name=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+4));
        //ASSERT(1==0);
        f->eax=create(file_name,*(unsigned *)(f->esp + 8));
        break;
     }
     case SYS_REMOVE:
     {
        //printf("remove\n");
        check_valid_esp((void *)(f->esp+4));
        check_valid_esp(*(void **)(f->esp+4));
        const char *file_name=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+4));
        f->eax=remove(file_name);
        break;
     }
     case SYS_OPEN:
     {
        //printf("open\n");
        check_valid_esp((void *)(f->esp+4));
        check_valid_esp(*(void **)(f->esp+4));
        const char *file_name=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+4));
        f->eax=open(file_name);
        //printf("end open\n");
        break;
     }
     case SYS_FILESIZE:
     {
        //printf("filesize\n");
        check_valid_esp((void *)(f->esp+4));
        f->eax=filesize(*(int *)(f->esp + 4));
        break;
     }
     case SYS_READ:
     {
        //printf("read\n");
        check_valid_esp((void *)(f->esp+4));
        check_valid_esp((void *)(f->esp+8));
        check_valid_esp((void *)(f->esp+12));
        unsigned size=*(unsigned *)(f->esp+12);
        int fd=*(int *)(f->esp+4);
        if(fd==0)
        {
           uint8_t *buffer_cpy=*(uint8_t **)(f->esp+8);
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy);
              buffer_cpy++;
           }
        }
        else
        {
           char *buffer_cpy=*(char **)(f->esp+8);
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy);
              buffer_cpy++;
           }
        }
        const void *buffer=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+8));
        f->eax=read (fd,buffer,size);
        //printf("end read\n");
        break;
     }
     case SYS_WRITE:
     {
        //printf("write\n");
   	check_valid_esp((void *)(f->esp+4));
        check_valid_esp((void *)(f->esp+8));
        check_valid_esp((void *)(f->esp+12));
        //printf("<2>\n");
        unsigned size=*(unsigned *)(f->esp+12);
        char *buffer_cpy=*(void **)(f->esp+8);
        unsigned i;
        for(i=0;i<size;i++)
        {
           check_valid_esp(buffer_cpy);
           buffer_cpy++;
        }
        const void *buffer=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+8));
        f->eax=write (*(int *)(f->esp+4),buffer,size);
        //printf("%d\n",f->eax);
        break;
     }
     case SYS_SEEK:
     {
        //printf("seek\n");
	check_valid_esp((void *)(f->esp+4));
        check_valid_esp((void *)(f->esp+8));
        seek(*(int *)(f->esp+4),*(unsigned *)(f->esp+8));
        break;
     }
     case SYS_TELL:
     {
        //printf("tell\n");
        check_valid_esp((void *)(f->esp+4));
        f->eax=tell(*(int *)(f->esp+4));
	break;
     }
     case SYS_CLOSE:
     {
        //printf("close\n");
        check_valid_esp((void *)(f->esp+4));
        close(*(int *)(f->esp+4));
	break;
     }
   }
}
void
check_valid_esp(const void *esp)
{

  if(esp >= PHYS_BASE || esp <= CODE_BASE)
   {
      exit(-1);
   }
  const void *ptr=pagedir_get_page(thread_current()->pagedir,esp);
  if(!ptr)
     exit(-1);
}

void halt(void)
{
   shutdown_power_off();
}

void exit(int status)
{
   struct thread *cur=thread_current();
   printf("%s: exit(%d)\n",cur->name,status);
   if(thread_exist(cur->parent))
   {
      cur->process->status=status;
      cur->process->exit=true;
   }
   //printf("pid:%d,status:%d\n",cur->process->pid,cur->process->status);
   thread_exit();
}

pid_t exec(const char*cmd_line)
{
   pid_t pid=process_execute(cmd_line);
   struct child_process_elem *child=get_child(pid);
   //printf("<3>\n");
   sema_down(&child->load_barrier);
   //printf("<22>\n");
   if(!child->load)
      return -1;
   return pid;
}

int wait(pid_t pid)
{
   return process_wait(pid);
}
bool create (const char *file, unsigned initial_size)
{
   if(!file)
      exit(-1);
   bool success = filesys_create (file, initial_size);
   //ASSERT(1==0);
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
   struct file_elem *fe= malloc(sizeof(struct file_elem));
   //ASSERT(0==1);
   list_push_back(&thread_current()->process->file_list,&fe->elem);
   //printf("%s,%d\n",thread_current()->name,list_size(&thread_current()->process->file_list));
   fe->fd=thread_current()->process->next_fd++;
   fe->file=file_ret;
   //ASSERT(0==1)
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
   //printf("%d\n",fd);
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
          free(list_entry(e,struct file_elem,elem));
          break;
      }
   }
   return;
}
void close_all(void)
{
   struct thread *cur=thread_current();
   while(!list_empty(&cur->process->file_list))
   {
      struct list_elem *e=list_pop_front(&cur->process->file_list);
      //printf("%d\n",list_size(&cur->process->file_list));
      file_close(list_entry(e,struct file_elem,elem)->file);
      free(list_entry(e,struct file_elem,elem));
   }
   return;
}
