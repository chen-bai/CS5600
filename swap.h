#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "vm/frame.h"
#include "vm/suppage.h"
#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"
struct shared_frame{
struct frame_elem *frame;
struct list spt_list;
};
bool load_back(struct sup_page_table *spt);
struct lock swap_lock;
struct block *swap_block;
struct bitmap *swap_bitmap;
struct list evict_list;
void init_swap(void);
void evict(void);
bool page_back(struct sup_page_table *spt);
#endif
