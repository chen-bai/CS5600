#ifndef VM_SUPPAGE_H
#define VM_SUPPAGE_H


#include "threads/palloc.h"
#include "threads/synch.h"
#include <stdbool.h>
#include <stdint.h>
#include <list.h>
#include "vm/frame.h"
struct sup_page_table{
   uint8_t *upage;
   struct frame_elem *frame;
   struct thread *thread;
   struct list_elem elem;
   struct list_elem felem;
};
struct sup_page_table *find_page(uint8_t *kpage);
struct sup_page_table *spt_init(uint8_t *upage, struct frame_elem *frame);
void spt_free(struct sup_page_table *spt);
struct sup_page_table *find_spt(uint8_t *upage);
#endif
