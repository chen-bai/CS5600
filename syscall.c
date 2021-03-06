#include "userprog/syscall.h"
#include <stdio.h>
#include <list.h>
#include "vm/suppage.h"
#include "vm/frame.h"
#include "vm/swap.h"
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
struct file_elem *find_mapped_file(struct file *file);
bool check_mmap_addr(void *upage);
void 
munmap(mapid_t mapid);
static void syscall_handler (struct intr_frame *);
void
check_valid_esp(const void *ptr,void *esp);
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
mapid_t mmap(int fd, void *upage);
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
  lock_init(&file_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //ASSERT(1==0);
  check_valid_esp((const void *)f->esp,thread_current()->esp);
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
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        exit(*(int *) (f->esp + 4));
        break;
     }
     case SYS_EXEC:
     {
        //printf("exec\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp(*(void **)(f->esp+4),thread_current()->esp);
        const char *cmd_line=*(char **)(f->esp+4);
        f->eax=exec(cmd_line);
        //printf("<pid>:%d\n",f->eax);
        break;
     }
     case SYS_WAIT:
     {
        //printf("%s:wait\n",thread_current()->name);
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        //printf("wait:%d\n",*(pid_t *) (f->esp + 4));
        f->eax=wait(*(pid_t *) (f->esp + 4));
        break;
     }
     case SYS_CREATE:
     {
        //printf("create\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp((void *)(f->esp+8),thread_current()->esp);
        check_valid_esp(*(void **)(f->esp+4),thread_current()->esp);
        const char *file_name=*(char **)(f->esp+4);
        //ASSERT(1==0);
        f->eax=create(file_name,*(unsigned *)(f->esp + 8));
        break;
     }
     case SYS_REMOVE:
     {
        //printf("remove\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp(*(void **)(f->esp+4),thread_current()->esp);
        const char *file_name=pagedir_get_page(thread_current()->pagedir,*(void **)(f->esp+4));
        f->eax=remove(file_name);
        break;
     }
     case SYS_OPEN:
     {
        //printf("open\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp(*(void **)(f->esp+4),thread_current()->esp);
        const char *file_name=*(char **)(f->esp+4);
        //printf("%s\n",);
        f->eax=open(file_name);
        //printf("end open\n");
        break;
     }
     case SYS_FILESIZE:
     {
        //printf("filesize\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        f->eax=filesize(*(int *)(f->esp + 4));
        break;
     }
     case SYS_READ:
     {
        //printf("read\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp((void *)(f->esp+8),thread_current()->esp);
        check_valid_esp((void *)(f->esp+12),thread_current()->esp);
        unsigned size=*(unsigned *)(f->esp+12);
        int fd=*(int *)(f->esp+4);
        if(fd==0)
        {
           uint8_t *buffer_cpy=*(uint8_t **)(f->esp+8);
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy,thread_current()->esp);
              buffer_cpy++;
           }
        }
        else
        {
           char *buffer_cpy=*(char **)(f->esp+8);
           unsigned i;
           for(i=0;i<size;i++)
           {
              check_valid_esp((void *)buffer_cpy,thread_current()->esp);
              buffer_cpy++;
           }
        }
        const void *buffer=*(void **)(f->esp+8);
        f->eax=read (fd,buffer,size);
        //printf("end read\n");
        break;
     }
     case SYS_WRITE:
     {
        //printf("write\n");
   	check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp((void *)(f->esp+8),thread_current()->esp);
        check_valid_esp((void *)(f->esp+12),thread_current()->esp);
        //printf("<2>\n");
        unsigned size=*(unsigned *)(f->esp+12);
        char *buffer_cpy=*(void **)(f->esp+8);
        unsigned i;
        for(i=0;i<size;i++)
        {
           check_valid_esp(buffer_cpy,thread_current()->esp);
           buffer_cpy++;
        }
        const void *buffer=*(void **)(f->esp+8);
        f->eax=write (*(int *)(f->esp+4),buffer,size);
        //printf("%d\n",f->eax);
        break;
     }
     case SYS_SEEK:
     {
        //printf("seek\n");
	check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        check_valid_esp((void *)(f->esp+8),thread_current()->esp);
        seek(*(int *)(f->esp+4),*(unsigned *)(f->esp+8));
        break;
     }
     case SYS_TELL:
     {
        //printf("tell\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        f->eax=tell(*(int *)(f->esp+4));
	break;
     }
     case SYS_CLOSE:
     {
        //printf("close\n");
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        close(*(int *)(f->esp+4));
	break;
     }
     case SYS_MMAP:
     {
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        f->eax=mmap(*(int *)(f->esp+4),*(uint32_t *)(f->esp+8));
        break;
     }
     case SYS_MUNMAP:
     {
        check_valid_esp((void *)(f->esp+4),thread_current()->esp);
        munmap(*(mapid_t *)(f->esp+4));
        break;
     }
   }
}
void
check_valid_esp(const void *ptr,void *esp)
{
  //printf("check!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
  if(ptr >= PHYS_BASE || ptr <= CODE_BASE)
   {
      exit(-1);
   }
  const void *addr=pagedir_get_page(thread_current()->pagedir,ptr);
  if(!addr)
  {
      //printf("here\n");
      void *upage=pg_round_down(ptr);
      struct sup_page_table *spt=find_spt(upage);
      if(spt)
      {
        bool success=load_back(spt);
        //ASSERT(0);
        if(!success)
           exit(-1);
        return;
      }
     else
     {
        if (ptr > esp - 33)
        {
           //printf("GROW STACK SC\n");
           void *kpage = frame_get_kpage (PAL_USER | PAL_ZERO);
           while(!kpage)
           {
              //printf("<2>\n");
              evict();
              kpage=palloc_get_page(PAL_USER | PAL_ZERO);
           }
           bool sucess=install_frame (pg_round_down(ptr),kpage,true,NULL,SWAP,NULL,NULL);
           //printf("%d\n",sucess);
           if(sucess)
           {
              thread_current()->esp=pg_round_down(ptr);
              return;
           }
           frame_destroy(kpage); 
        }
     }
     exit(-1);
  }
  //printf("sb\n");	
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
   lock_acquire(&file_lock);
   bool success = filesys_create (file, initial_size);
   lock_release(&file_lock);
   return success;   
}
bool remove (const char *file)
{
   bool success = filesys_remove (file);
   return success;
}
int open (const char *file)
{  
   lock_acquire(&file_lock);
   //printf("%s\n",file);
   struct file *file_ret=filesys_open (file);
   //printf("%u\n",file_ret);
   if(file_ret==NULL)
   {
      lock_release(&file_lock);
      return -1;
   }
   struct file_elem *fe= malloc(sizeof(struct file_elem));
   //ASSERT(0==1);
   list_push_back(&thread_current()->process->file_list,&fe->elem);
   //printf("%s,%d\n",thread_current()->name,list_size(&thread_current()->process->file_list));
   fe->fd=thread_current()->process->next_fd++;
   fe->file=file_ret;
   //ASSERT(0==1)
   lock_release(&file_lock);
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
   lock_acquire(&file_lock);
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
   {
      lock_release(&file_lock);
      return -1;
   }
   int ret= file_read(file, buffer, size);
   lock_release(&file_lock);
   return ret;
}
int write (int fd, const void *buffer, unsigned length)
{
   unsigned size=length;
   if(fd==1)
   {
      putbuf(buffer, size);
      return size;
   }
   lock_acquire(&file_lock);
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
   {
      lock_release(&file_lock);  
      return -1;
   }
   int ret= file_write(file, buffer, size);
   lock_release(&file_lock);
   return ret;
}
void seek (int fd, unsigned position)
{
   lock_acquire(&file_lock);
   struct thread *cur=thread_current();
   struct file *file=find_file(cur->process,fd);
   if(!file)
   {
      lock_release(&file_lock);
      return;
   }
   file_seek(file, position);
   lock_release(&file_lock);
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
          lock_acquire(&file_lock);
	  file_close(list_entry(e,struct file_elem,elem)->file);
	  list_remove(e);
          free(list_entry(e,struct file_elem,elem));
          lock_release(&file_lock);
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
bool check_mmap_addr(void *upage)
{
   if(upage>=PHYS_BASE||upage<CODE_BASE||pg_round_down(upage) != upage)
   {
      return false;
   }
   if(find_spt(upage))
      return false;
   return true;
}
mapid_t mmap(int fd, void *upage)
{
    //printf("mmap\n");
    if(!check_mmap_addr(upage))
       return -1;
    lock_acquire(&file_lock);
    int length = filesize (fd);
    if(length<=0)
      return -1;
    struct file *mmap_file=find_file(thread_current()->process,fd);
    struct file *file=file_reopen(mmap_file);
    if(!file)
    {
      return -1;
    }
    file_seek(file,0);
    off_t ofs = 0;
    int read_bytes = length;
    int zero_bytes = 0;
    mapid_t mapid = thread_current ()->next_mapid++;
    bool writable=!(file->deny_write);
    while (read_bytes > 0||zero_bytes >0)
    {
       uint32_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
       uint32_t page_zero_bytes = PGSIZE - page_read_bytes;
       //printf("%d\n",read_bytes);
       bool success = install_frame(upage,NULL,writable,file,MMAP,ofs,page_read_bytes);
       
	if (!success)
	  {
	    munmap (mapid);
            return -1;
	  }
	else
	  {
	    struct mmap_elem * mmap_elem =
	    (struct mmap_elem *) malloc (sizeof(struct mmap_elem));
	    mmap_elem->mapid = mapid;
	    mmap_elem->file = file;
	    mmap_elem->spt = find_spt (upage);
            mmap_elem->writable=!(file->deny_write);
	    list_push_back (&thread_current ()->mmap_list,
		&mmap_elem->elem);
	  }
        
	zero_bytes -= page_zero_bytes;
	read_bytes -= page_read_bytes;
	ofs += page_read_bytes;
	upage += PGSIZE;
    }
    struct file_elem *mf=malloc(sizeof(struct file_elem));
    mf->file=file;
    list_push_back(&thread_current()->mmap_file_list,&mf->elem);
    lock_release(&file_lock);
    //printf("mapid:%d\n",mapid);
    return mapid;
}

void munmap(mapid_t mapid)
{
   struct thread *cur=thread_current();
   struct list_elem *e=list_begin(&cur->mmap_list);
   //printf("<5>\n");
   while(e!=list_end(&cur->mmap_list))
   {
      //printf("<6>\n");
      struct mmap_elem *mmap=list_entry(e,struct mmap_elem,elem);
      e=list_next(e);
      if(mapid==mmap->mapid)
      {
         struct frame_elem *frame=mmap->spt->frame;
         if(frame->load)
         {
            if(pagedir_is_dirty(thread_current()->pagedir,mmap->spt->upage))
            {
               //printf("sb\n");
	       int n=file_write_at (frame->file,frame->kpage,frame->page_read_bytes,frame->ofs); 
               //printf("%d\n",n);             
            }
         pagedir_clear_page(thread_current()->pagedir,mmap->spt->upage);  
         }
         spt_free(mmap->spt);
         frame_free(frame);
         list_remove(&mmap->elem);
         struct file_elem *fe=find_mapped_file(mmap->file);
         //printf("<8>\n");
         if(fe)
         {
            file_close(fe->file);
            list_remove(&fe->elem);
            free(fe);
         }
         free(mmap);
      }
   } 
   //printf("<7>\n");
   return;
}
void munmap_all(void)
{
   struct thread *cur=thread_current();
   while(!list_empty(&cur->mmap_list))
   {
      struct list_elem *e=list_pop_front(&cur->mmap_list);
      struct mmap_elem *mmap=list_entry(e,struct mmap_elem,elem);
      struct frame_elem *frame=mmap->spt->frame;
      if(frame->load)
      {
         if(pagedir_is_dirty(thread_current()->pagedir,mmap->spt->upage))
         {
	    file_write_at (frame->file,frame->kpage,frame->page_read_bytes,frame->ofs);
         } 
         pagedir_clear_page(thread_current()->pagedir,mmap->spt->upage); 
      }
      spt_free(mmap->spt);
      frame_free(frame);    
      struct file_elem *fe=find_mapped_file(mmap->file);
      if(fe)
      {
         list_remove(&fe->elem);
         free(fe);
      }
      free(mmap);
   }
   //printf("abcd\n");
   return;
}
struct file_elem *find_mapped_file(struct file *file)
{
   struct thread *cur=thread_current();
   struct list_elem *e;
   struct file_elem *result=NULL;
   for(e=list_begin(&cur->mmap_file_list);e!=list_end(&cur->mmap_file_list);e=list_next(e))
   {
      struct file_elem *fe=list_entry(e,struct file_elem,elem);
      if(fe->file->inode==file->inode)
      {
         result=fe;
         break;
      }
   }
   return result;
}
