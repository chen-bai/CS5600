#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <list.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "filesys/inode.h"

#define CACHE_SIZE 64
#define READ_BLOCK 0
#define WRITE_BLOCK 1

struct lock cache_lock;
struct list cache_list;
int used_cache;
struct cache
  {
    block_sector_t sid;
    bool AB;
    bool DB;
    int read_cnt;
    struct lock io;
    struct condition read_end;
    uint8_t data[BLOCK_SECTOR_SIZE];
    struct list_elem elem;
  };
void cache_system_init(void);
struct cache * find_cache(block_sector_t sid);
struct cache *cache_block(block_sector_t sid,bool type);
struct cache *create_cache(block_sector_t sid,bool type);
void cache_writeback_func (void *aux UNUSED);
#endif
