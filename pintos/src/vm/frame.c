#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/suppage.h"
#include "vm/swap.h"
#include <list.h>
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"

void frame_table_init(void)
{
list_init(&frame_table);
//list_init(&evict_list);
return;
}

void *frame_get_kpage(enum palloc_flags flags)
{
   if((flags & PAL_USER)==0)
      return NULL;
   void *kpage=palloc_get_page(flags);
   return kpage;
}
void frame_destroy(void *kpage)
{
   //printf("frame_destroy\n");
      //if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
   struct list_elem *e=NULL;
   for(e=list_begin(&frame_table);e!=list_end(&frame_table);e=list_next(e))
   {
      struct frame_elem *fe=list_entry(e,struct frame_elem,elem);
      if(fe->kpage==kpage)
      {
         //printf("remove\n");
         list_remove(e);
         //printf("remove end\n");
         //frame_clear(fe);
         free(fe);
         palloc_free_page(kpage);
         break;
      }
   }
   lock_release(&frame_lock);
}
struct frame_elem *frame_find(void *kpage)
{
   //printf("frame_destroy\n");
      //if(frame_lock.holder!=thread_current())
   lock_acquire(&frame_lock);
   struct list_elem *e=NULL;
   struct frame_elem *frame=NULL;
   for(e=list_begin(&frame_table);e!=list_end(&frame_table);e=list_next(e))
   {
      struct frame_elem *fe=list_entry(e,struct frame_elem,elem);
      if(fe->kpage==kpage)
      {
         frame=fe;
         break;
      }
   }
   lock_release(&frame_lock);
   return frame;
}
void frame_free(struct frame_elem *frame)
{
   //printf("frame_free\n");
   if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
   if(frame->shared==NULL)
   {
   if(frame->type!=FILE)
   {
      if(frame->load)
      {
         list_remove(&frame->elem);
         palloc_free_page(frame->kpage);
      }
      else
      {
         //printf("<1>\n");
         if(frame->index!=-1)
         {
            lock_acquire(&swap_lock);
            bitmap_flip(swap_bitmap, frame->index);
            lock_release(&swap_lock);
         }
         //printf("<2>\n");
      }
   }
   else
   {
      if(frame->load)
      {
         list_remove(&frame->elem);
         palloc_free_page(frame->kpage);
      }
   }
   free(frame);
   }
   lock_release(&frame_lock);
}
/*
void frame_clear(struct frame_elem *frame)
{
   while(!list_empty(&frame->spt_list))
   {
      struct list_elem *e=list_begin(&frame->spt_list);
      struct sup_page_table *spt=list_entry(e,struct sup_page_table,felem);
      spt_free(spt);  
   }
}
*/
struct frame_elem *frame_table_insert(void *kpage, struct file *file,enum page_type type,off_t ofs,size_t page_read_bytes,bool writable)
{
  //printf("frame_insert\n");
  struct frame_elem *fe=malloc(sizeof(struct frame_elem));
  fe->kpage=kpage;
  //printf("kpage:%u\n",kpage);
  if(type==FILE||type==MMAP)
     fe->load=false;
  else
     fe->load=true;
  fe->type=type;
  fe->index=-1;
  if(type==FILE||type==MMAP)
  {
     fe->ofs=ofs;
     fe->file=file;
     fe->page_read_bytes=page_read_bytes;
  }
  else
  {
        //if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
     list_push_back(&frame_table,&fe->elem);
     lock_release(&frame_lock);
  }
  fe->writable=writable;
  fe->shared=NULL;
  //list_init(&fe->spt_list);
  return fe;
}
struct frame_elem *check_match(struct file *file,off_t ofs,size_t page_read_bytes)
{
   //printf("<2>\n");
      lock_acquire(&frame_lock);
   struct frame_elem *frame=NULL;
   struct list_elem *e=NULL;
   //printf("<3>\n");
   for(e=list_begin(&frame_table);e!=list_end(&frame_table);e=list_next(e))
   {
      struct frame_elem *fe=list_entry(e,struct frame_elem,elem);
      struct thread *t=fe->spt->thread;
      ASSERT(t!=NULL);
      if(fe->file!=NULL&&file!=NULL)
      {
      //printf("<4>\n");
      ASSERT(fe->kpage);
      if(!pagedir_is_dirty(t->pagedir,fe->kpage))
      {
      if(fe->file->inode==file->inode&&fe->ofs==ofs&&fe->page_read_bytes==page_read_bytes)
      {
         //printf("<@>\n");
         frame=fe;
         break;
      }
      }
      }
   }
   //printf("<4>\n");
   lock_release(&frame_lock);
   return frame;
}
