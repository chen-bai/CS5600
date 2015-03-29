#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "vm/suppage.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/thread.h"
struct sup_page_table *spt_init(uint8_t *upage, struct frame_elem *frame)
{
   //printf("spt_init\n");
   struct sup_page_table *spt=malloc(sizeof(struct sup_page_table));
   spt->upage=upage;
   spt->frame=frame;
   /*
   if(frame_lock.holder!=thread_current())
      lock_acquire(&frame_lock);
   list_push_back(&frame->spt_list,&spt->felem);
   */
   frame->spt=spt;
   //lock_release(&frame_lock);
   list_push_back(&thread_current()->page_table,&spt->elem);
   spt->thread=thread_current();
   return spt;
}
void spt_free(struct sup_page_table *spt)
{
   //printf("spt_free\n");
   if(spt->frame->load)
      pagedir_clear_page (thread_current ()->pagedir,spt->upage); 
   list_remove(&spt->elem);
   //printf("<2>\n");
   if(spt->frame->shared!=NULL)
   {
      if(frame_lock.holder!=thread_current())
          lock_acquire(&frame_lock);
      list_remove(&spt->felem);
      if(list_empty(&spt->frame->shared->spt_list))
      {
         free(spt->frame->shared);
         spt->frame->shared=NULL;
      }
      else if(spt->frame->spt==spt)
      {
         struct list_elem *e=list_begin(&spt->frame->shared->spt_list);
         struct sup_page_table *page=list_entry(e,struct sup_page_table,felem);
         spt->frame->spt=page;
      }
      lock_release(&frame_lock);
   }
   //printf("<3>\n");
   free(spt);
   return;
}

struct sup_page_table *find_spt(uint8_t *upage)
{
   struct list_elem *e=NULL;
   struct sup_page_table *result=NULL;
   for(e=list_begin(&thread_current()->page_table);e!=list_end(&thread_current()->page_table);e=list_next(e))
   {
      struct sup_page_table *spt=list_entry(e,struct sup_page_table,elem);
      if(spt->upage==upage)
      {
         result=spt;
         break;
      }
   }
   return result;
}
struct sup_page_table *find_page(uint8_t *kpage)
{
   struct list_elem *e=NULL;
   struct sup_page_table *result=NULL;
   for(e=list_begin(&thread_current()->page_table);e!=list_end(&thread_current()->page_table);e=list_next(e))
   {
      struct sup_page_table *spt=list_entry(e,struct sup_page_table,elem);
      if(spt->frame->load)
      {
      if(spt->frame->kpage==kpage)
      {
         result=spt;
         break;
      }
      }
   }
   return result;
}
