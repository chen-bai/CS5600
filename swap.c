#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "vm/frame.h"
#include "vm/suppage.h"
#include "vm/swap.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include <bitmap.h>
#include "userprog/syscall.h"

void init_swap(void)
{
   swap_block = block_get_role (BLOCK_SWAP);
   swap_bitmap = bitmap_create( block_size(swap_block) /(PGSIZE/BLOCK_SECTOR_SIZE));
   bitmap_set_all(swap_bitmap, false);
   lock_init(&swap_lock);
   lock_init(&frame_lock);
}
size_t sout(struct frame_elem *frame)
{
   //printf("sout\n");
      //if(swap_lock.holder!=thread_current())
      lock_acquire(&swap_lock);
   frame->type=SWAP;
      //printf("<sb>\n");
   size_t index=bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
   //printf("%d\n",index);
   void *kpage=frame->kpage;
   size_t i;
   for(i=0;i<(PGSIZE/BLOCK_SECTOR_SIZE);i++)
    {
       block_write(swap_block, index * (PGSIZE/BLOCK_SECTOR_SIZE) + i,
       (uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
    }
   lock_release(&swap_lock);
   return index;
}
void sback(size_t index,struct frame_elem *frame)
{
   //printf("sback\n");
      //if(swap_lock.holder!=thread_current())
      lock_acquire(&swap_lock);
   bitmap_flip(swap_bitmap, index);
   size_t i;
   //printf("%d\n",index);
   void *kpage=frame->kpage;
   
   for(i=0;i<(PGSIZE/BLOCK_SECTOR_SIZE);i++)
   {
      block_read(swap_block,index* (PGSIZE/BLOCK_SECTOR_SIZE)+i,(uint8_t *) kpage + i * BLOCK_SECTOR_SIZE);
   }
   
   //printf("end sback\n");
   lock_release(&swap_lock);
}
void evict(void)
{
   //printf("frame:%d\n",list_size(&frame_table));
   struct list_elem *e=NULL;
   struct frame_elem *evict_frame=NULL;
      //if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
   for(e=list_begin(&frame_table); e!=list_end(&frame_table); e=list_next(e))
   {
      struct frame_elem *frame=list_entry(e,struct frame_elem,elem);
      if(pagedir_is_accessed(thread_current()->pagedir,frame->spt->upage))
      {
         pagedir_set_accessed(thread_current()->pagedir,frame->spt->upage,false);
         continue;
      }
      if(pagedir_is_dirty(thread_current()->pagedir,frame->spt->upage))
      {
         continue;
      }
      evict_frame=frame;
      //printf("here\n");
      break;
   }
   if(!evict_frame)
      evict_frame=list_entry(list_begin(&frame_table),struct frame_elem,elem);
   ASSERT(evict_frame);
   //if(evict_frame->type==STACK)
      //printf("STACK\n");
   list_remove(&evict_frame->elem);
   //list_push_back(&evict_list,&evict_frame->elem);
   evict_frame->load=false;
   
   if(evict_frame->shared)
   {
      //printf("shared evict\n");
      palloc_free_page(evict_frame->kpage);
      struct list_elem *e;
      struct list l=evict_frame->shared->spt_list;
      for(e=list_begin(&l);e!=list_end(&l);e=list_next(e))
      {
         struct sup_page_table *spt=list_entry(e,struct sup_page_table,felem);
         pagedir_clear_page (spt->thread->pagedir, spt->upage);
      }
      return;
   }
   //file_write_at(evict_frame->file,evict_frame->kpage,evict_frame->page_read_bytes,evict_frame->ofs);
   if(evict_frame->type==MMAP)
   {     
      if(pagedir_is_dirty(thread_current()->pagedir,evict_frame->spt->upage))
      {
         if(file_lock.holder!=thread_current())
            lock_acquire(&file_lock);
         file_write_at (evict_frame->file,evict_frame->kpage,evict_frame->page_read_bytes,evict_frame->ofs); 
         lock_release(&file_lock);
      }
   }
   else
   {
      if(evict_frame->type!=FILE||pagedir_is_dirty(thread_current()->pagedir,evict_frame->spt->upage))
      {
         evict_frame->type=SWAP;
         evict_frame->index=sout(evict_frame);
      }
   }
   pagedir_clear_page(evict_frame->spt->thread->pagedir,evict_frame->spt->upage);
   palloc_free_page(evict_frame->kpage);
   /*
   for(e=list_begin(&evict_frame->spt_list);e!=list_end(&evict_frame->spt_list);e=list_next(e))
   {
      struct sup_page_table *spt=list_entry(e,struct sup_page_table,felem);
      printf("%u\n",spt->upage);
      pagedir_clear_page (spt->thread->pagedir, spt->upage);
      printf("<4>\n");
   }
   */
   lock_release(&frame_lock);
   //printf("<3>\n");
   return;
}


bool page_back(struct sup_page_table *spt)
{
   //printf("page_back\n");
   
   //ASSERT(swap_sema.value==1);
   struct thread *t=thread_current();
   struct frame_elem *frame=spt->frame;
   void *upage=spt->upage;
   void *kpage=frame_get_kpage(PAL_USER);
   while(!kpage)
   {
      //printf("x\n");
      evict();
      kpage=frame_get_kpage(PAL_USER);
   }
   ASSERT(kpage);
      //if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
   frame->kpage=kpage;
   /*if (file_read_at(frame->file, kpage, frame->page_read_bytes,frame->ofs) != (int)frame-> page_read_bytes)
   {
      palloc_free_page(kpage);
      return false; 
   }
   */
   //printf("<999>\n");
   sback(frame->index,frame);
   bool success=false;
   /*
   struct list_elem *e;
   for(e=list_begin(&frame->spt_list);e!=list_end(&frame->spt_list);e=list_next(e))
   {
      struct sup_page_table *page=list_entry(e,struct sup_page_table,felem);
      struct thread *t =page->thread;
      success=(pagedir_get_page (t->pagedir, page->upage) == NULL
          && pagedir_set_page (t->pagedir, page->upage, kpage, frame->writable));
      if(!success)
         break;
   }
   */
   //printf("<000>\n");
   success=install_page(spt->upage,frame->kpage,frame->writable);
   if(success)
   {
         //list_remove(&frame->elem);
         list_push_back(&frame_table,&frame->elem);
         frame->load=true;
   }
   //printf("<a>\n");
   lock_release(&frame_lock);
   return true;
}
bool file_back(struct sup_page_table *spt)
{
   enum palloc_flags flags = PAL_USER;
   if(spt->frame->page_read_bytes==0)
   {
      //printf("<@>\n");
      flags |=PAL_ZERO;
   }
   
   //printf("<7>\n");
   void *kpage=frame_get_kpage(flags);
   //printf("<8>\n");
   while(!kpage)
   {
      evict();
      //printf("<9>\n");
      kpage=frame_get_kpage(flags);
   }
   //printf("<10>\n");
   struct frame_elem *frame=spt->frame;
   frame->kpage=kpage;
   //printf("%u\n",kpage);
   ASSERT(is_kernel_vaddr(frame->kpage));
   if(frame->page_read_bytes>0)
   {
      if(file_lock.holder!=thread_current())
         lock_acquire(&file_lock);
      if (file_read_at(frame->file, kpage, frame->page_read_bytes,frame->ofs) != (int)frame-> page_read_bytes)
      {
         //printf("%u\n",kpage);
         palloc_free_page(kpage);
         lock_release(&file_lock);
         return false; 
      }
      memset(frame->kpage + frame->page_read_bytes, 0, (PGSIZE - frame->page_read_bytes));
      lock_release(&file_lock);
   }
   bool success=false;
   
   if(frame->shared)
   {
      //printf("shared\n");
      struct list_elem *e;
      struct list l=frame->shared->spt_list;
      for(e=list_begin(&l);e!=list_end(&l);e=list_next(e))
      {
         struct sup_page_table *page=list_entry(e,struct sup_page_table,felem);
         success=(pagedir_get_page (page->thread->pagedir, page->upage)== NULL && pagedir_set_page (page->thread->pagedir, page->upage,kpage, false));
      }
      return success;
   }
   //printf("<2>\n");
   /*
   struct list_elem *e;
   for(e=list_begin(&frame->spt_list);e!=list_end(&frame->spt_list);e=list_next(e))
   {
      struct sup_page_table *page=list_entry(e,struct sup_page_table,felem);
      struct thread *t =page->thread;
      success=(pagedir_get_page (t->pagedir, page->upage) == NULL && pagedir_set_page (t->pagedir, page->upage, kpage, frame->writable));
      if(!success)
            break;
   }*/
   
   success=install_page(spt->upage,frame->kpage,frame->writable);
      //printf("%d\n",success);
   if(success)
   {
         //if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
      frame->load=true;
      list_push_back(&frame_table,&frame->elem);
      lock_release(&frame_lock);
   }
   return success;
}
bool load_back(struct sup_page_table *spt)
{
   //printf("load back\n");
   if(!spt)
   {
      //printf("<no spt\n>");
      return false;
   }
   //ASSERT(0);
   switch(spt->frame->type)
   {
   case FILE:
   {
      //printf("FILE\n");
      return file_back(spt);
      break;
   }
   case MMAP:
   {
      //printf("MMAP\n");
      return file_back(spt);
      break;
   }
   case SWAP:
   {
      //printf("SWAP\n");
      if(page_back(spt))
         return true;
      else
         return false;
      break;
   }
   }
}

