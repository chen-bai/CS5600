#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "vm/suppage.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
struct lock frame_lock;
enum page_type{
   FILE,
   MMAP,
   SWAP,
   SHARE
};
struct frame_elem{
   void *kpage;
   struct sup_page_table *spt;
   struct shared_frame *shared;
   //struct list spt_list;
   bool load;
   struct file *file; 
    
   size_t index;

   struct list_elem elem;
 
   enum page_type type;
   off_t ofs;
   size_t page_read_bytes;
   bool writable;

};
struct frame_elem *frame_find(void *kpage);
struct frame_elem *check_match(struct file *file,off_t ofs,size_t page_read_bytes);
struct list frame_table;
void frame_table_init(void);
void *frame_get_kpage(enum palloc_flags flags);
void frame_destroy(void *kpage);
void frame_clear(struct frame_elem *frame);
struct frame_elem *frame_table_insert(void *kpage, struct file *file, enum page_type type,off_t ofs,size_t page_read_bytes,bool writable);
//struct frame_elem *frame_table_insert(void *kpage);
void frame_free(struct frame_elem *frame);
#endif
