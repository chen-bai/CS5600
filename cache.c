#include <stdbool.h>
#include <list.h>
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"

void cache_system_init(void)
{
   list_init(&cache_list);
   lock_init(&cache_lock);
   used_cache=0;
   thread_create("cwf",PRI_DEFAULT,cache_writeback_func,NULL);
}
void read_cache(struct cache *cache)
{
   lock_acquire(&cache->io);
   cache->read_cnt++;
   lock_release(&cache->io);
}
void end_read(struct cache *cache)
{
   lock_acquire(&cache->io);
   cache->read_cnt--;
   if(cache->read_cnt==0)
     cond_signal(&cache->read_end,&cache->io);
   lock_release(&cache->io);
}
void write_cache(struct cache *cache)
{
   lock_acquire(&cache->io);
   if(cache->read_cnt>0)
      cond_wait(&cache->read_end,&cache->io);
}
void end_write(struct cache *cache)
{
   cond_signal(&cache->read_end,&cache->io);
   lock_release(&cache->io);
}
struct cache * find_cache(block_sector_t sid)
{
   int cl=0;
   if(cache_lock.holder!=thread_current())
   { 
      cl=1;
      lock_acquire(&cache_lock);
   }
   struct list_elem *e;
   for(e=list_begin(&cache_list);e!=list_end(&cache_list);e=list_next(e))
   {
      struct cache *cache=list_entry(e,struct cache,elem);
      if(cache->sid==sid)
      {
         if(cl==1)
             lock_release(&cache_lock);
         return cache;
      }
   }
   if(cl==1)
       lock_release(&cache_lock);
   return NULL;
}

struct cache *cache_block(block_sector_t sid,bool type)
{
   struct cache *c=find_cache(sid);
   //printf("find end\n");
   if(!c)
   {
      //printf("cre\n");
      c=create_cache(sid,type);
      //printf("cre end\n");
   }
   else
   {
      lock_acquire(&c->io);
      c->AB=true;
      c->DB|=type;
      lock_release(&c->io);
   }
   //printf("ret\n");
   return c;
}

struct cache *create_cache(block_sector_t sid,bool type)
{  
   int cl=0;
   if(cache_lock.holder!=thread_current())
   { 
      cl=1;
      lock_acquire(&cache_lock);
   }
   //printf("cre2\n");
   while(used_cache>=CACHE_SIZE)
   {
      cache_evict();
   }
   //printf("cre3\n");
   struct cache *c=malloc(sizeof(struct cache));
   list_push_back(&cache_list,&c->elem);
   //printf("create:%d,%d,%d\n",sid,type,list_size(&cache_list));
   used_cache++;
   lock_init(&c->io);
   cond_init(&c->read_end);
   c->read_cnt=0;
   lock_acquire(&c->io);
   c->AB=true;
   c->sid=sid;
   block_read(fs_device,c->sid,&c->data);
   lock_release(&c->io);
   c->DB=type;
   if(cl==1)
      lock_release(&cache_lock);
   return c;
}

void cache_evict(void)
{  
   struct list_elem *e;
   while(1)
   {
      //printf("evict:%d\n",list_size(&cache_list));
      for(e=list_begin(&cache_list);e!=list_end(&cache_list);)
      {
         //printf("iter\n");
         struct list_elem *next=list_next(e);
         struct cache *c=list_entry(e,struct cache,elem);
         int cio=0;
         if(c->io.holder!=thread_current())
         {
            cio=1;
            lock_acquire(&c->io);
         }
         if(c->AB==true)
         {
            c->AB=false;
            e=next;
            if(cio==1)
               lock_release(&c->io);
            continue;
         }
         //printf("iter end\n");
         if(c->DB==true)
         {
            block_write(fs_device,c->sid,&c->data);
         }
         if(cio==1)
             lock_release(&c->io);
         list_remove(e);
         free(c);
         goto done;
      }
   }
done:
   used_cache--;
   return;
}

void cache_writeback_func (void *aux UNUSED)
{
   while(1)
   {
      timer_sleep(100);
      //printf("cwf up\n");
      lock_acquire(&cache_lock);
      write_to_disk();
      lock_release(&cache_lock);
   }
}


void write_to_disk(void)
{
   struct list_elem *e;
   for(e=list_begin(&cache_list);e!=list_end(&cache_list);)
   {
      struct list_elem *next=list_next(e);
      struct cache *c= list_entry(e,struct cache,elem);
      lock_acquire(&c->io);
      if(c->DB==true)
      {
         block_write(fs_device,c->sid,&c->data);
         c->DB=false;
      }
      lock_release(&c->io);
      e=next;
   }
}
void cache_remove(struct cache *cache)
{
   lock_acquire(&cache_lock);
   list_remove(&cache->elem);
   free(cache);
   lock_release(&cache_lock);
}

void free_cache(void)
{
   lock_acquire(&cache_lock);
   struct list_elem *e=list_begin(&cache_list);
   while(e!=list_end(&cache_list))
   {
      struct list_elem *next=list_next(e);
      struct cache *c= list_entry(e,struct cache,elem);
      lock_acquire(&c->io);
      block_write(fs_device,c->sid,&c->data);
      lock_release(&c->io);
      list_remove(e);
      free(c);
      e=next;
      used_cache--;
   }
   lock_release(&cache_lock);
}

