#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
struct inode_disk
  {
    off_t length; /* File size in bytes. */
    block_sector_t sp; /* Single level pointer. */
    block_sector_t ind; /* Double level pointer. */
    block_sector_t din; /* Double level pointer. */
    unsigned magic; /* Magic number. */
    bool isdir; /* is directory */
    block_sector_t parent; /* parent directory */   
    uint32_t unused[121];  
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */

    int read_cnt;
    struct condition read_end;
    struct lock io;
  };

void read_inode(struct inode *inode)
{
  lock_acquire(&inode->io);
  inode->read_cnt++;
  lock_release(&inode->io);
}
void read_end(struct inode *inode)
{
  lock_acquire(&inode->io);
  inode->read_cnt--;
  if(inode->read_cnt==0)
     cond_signal(&inode->read_end,&inode->io);
  lock_release(&inode->io);
}
void write_inode(struct inode *inode)
{
  //printf("here2\n");
  ASSERT(inode->io.holder!=thread_current())
  lock_acquire(&inode->io);
  //printf("here3\n");
  if(inode->read_cnt>0)
     cond_wait(&inode->read_end,&inode->io);
  //printf("here4\n");
}
void write_end(struct inode *inode)
{
  cond_signal(&inode->read_end,&inode->io);
  lock_release(&inode->io);
}
bool
inode_isdir (struct inode* inode)
{
  if (inode != NULL)
  {
     lock_acquire(&inode->io);
     bool ret=inode->data.isdir;
     lock_release(&inode->io);
     return ret;
  }
  return false;
}
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

struct lock inode_lock;

int inode_open_cnt(struct inode *inode)
{
  lock_acquire(&inode->io);
  int ret=inode->open_cnt;
  lock_release(&inode->io);
  return ret;
}
struct index
  {
    block_sector_t block[128];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

block_sector_t inode_parent(struct inode *inode)
{
  lock_acquire(&inode->io);
  block_sector_t ret=inode->data.parent;
  lock_release(&inode->io);
  return ret;
}
void inode_set_parent(struct inode *inode,block_sector_t parent)
{
  lock_acquire(&inode->io);
  inode->data.parent=parent;
  struct cache *cache=cache_block(inode->sector,WRITE_BLOCK);
  write_cache(cache);
  memcpy((uint8_t *)&cache->data,&inode->data,BLOCK_SECTOR_SIZE);
  end_write(cache);
  lock_release(&inode->io);
}
block_sector_t inode_sid(struct inode *inode)
{
  lock_acquire(&inode->io);
  block_sector_t ret=inode->sector;
  lock_release(&inode->io);
  return ret;
}
void inode_delete(struct inode *inode)
{
  //printf("delete\n");
  lock_acquire(&inode->io);
  int length = inode->data.length;
  int block_current = inode->data.length / BLOCK_SECTOR_SIZE;
  free_map_release(inode->data.sp,1);
  struct cache *c=find_cache(inode->data.sp);
  if(c)
  {
     cache_remove(c);
  }

  if(block_current>=1&&length!=512)
  {
     struct cache *cache=cache_block(inode->data.ind,READ_BLOCK);
     free_map_release(inode->data.ind,1);
     struct index *index=calloc(1,sizeof *index);
     read_cache(cache);
     memcpy(index,&cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     cache_remove(cache);
     int i=0;
     for(i=0;i<block_current;i++)
     {
        free_map_release(index->block[block_current+i-1],1);
        struct cache *c=find_cache(index->block[block_current+i-1]);
        if(c)
        {
           cache_remove(c);
        }
     }
     free(index);
  }
  if(block_current>=129&&length!=129*512)
  {
     free_map_release(inode->data.din,1);
     struct cache *cache=cache_block(inode->data.din,READ_BLOCK);
     struct index *index=calloc(1,sizeof *index);
     read_cache(cache);
     memcpy(index,&cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     cache_remove(cache);
     int i,j=0;
     int m=(block_current-129)/128;
     int n=(block_current-129)%128;
     for(i=0;i<m+1;i++)
     {
        struct cache *c=cache_block(index->block[i],READ_BLOCK);
        struct index *d_index=calloc(1,sizeof *index);
        read_cache(cache);
        memcpy(d_index,&c->data,BLOCK_SECTOR_SIZE);
        end_read(cache);
        free_map_release(index->block[i],1);
        cache_remove(c);
        int high=(i==m?n+1:128);
        for(j=0;j<high;j++)
        {
           free_map_release(d_index->block[j],1);
           struct cache *ca=find_cache(index->block[j]);
           if(ca)
           {
              cache_remove(ca);
           }
        }
        free(d_index);
     }
     free(index);
  }
  lock_release(&inode->io);
  return;
}

static block_sector_t file_growth(struct inode *inode, off_t pos,off_t size)
{
  int block = (pos + size) / BLOCK_SECTOR_SIZE;
  //printf("file growth froem %d to %d\n",inode->data.length,pos+size);
  int length = inode->data.length;
  int block_current = inode->data.length / BLOCK_SECTOR_SIZE;
  off_t block_ofs= (pos + size) % BLOCK_SECTOR_SIZE;
  inode->data.length=pos+size;
  if ((block==block_current)&&(length%BLOCK_SECTOR_SIZE!=0))
  {
     int bytes_write = pos - length;
     block_sector_t sid=byte_to_sector(inode,length);
     struct cache *cache = cache_block (sid, WRITE_BLOCK);
     write_cache(cache);
     memset((uint8_t *)&cache->data+length,0,bytes_write);
     end_write(cache);
     return block;
  }
  if ( length ==0)
  {
     free_map_allocate(1,&inode->data.sp);
  }
  if(block_current<1)
  {
     int bytes_write = 512 - length;
     struct cache *cache = cache_block (inode->data.sp, WRITE_BLOCK);
     write_cache(cache);
     memset((uint8_t *)&cache->data+length,0,bytes_write);
     end_write(cache);
     length += bytes_write;
     block_current++;
  }
  if(block==1 && block_ofs==0)
     return block;
  if(length==512)
  {
     free_map_allocate(1,&inode->data.ind);
  }
  if( block < 129 )
  {
     //printf("b:%d\n",block_current);
     if( length % BLOCK_SECTOR_SIZE !=0)
     {
        //printf("locked\n");
        struct cache *cache = cache_block (inode->data.ind, READ_BLOCK);
        struct index *index = calloc(1,sizeof *index);
        //printf("cached\n");
        read_cache(cache);
        memcpy(index, (uint8_t *)&cache->data,BLOCK_SECTOR_SIZE);
        end_read(cache);
        cache = cache_block (index->block[block_current-1], WRITE_BLOCK);
        //printf("cached\n");
        int start=length % BLOCK_SECTOR_SIZE;
        write_cache(cache);
        memset((uint8_t *)&cache->data+start,0,BLOCK_SECTOR_SIZE-start);
        end_write(cache);
        free(index);
        block_current++;
     }
     //printf("finish\n");
     int blocks=block-block_current+1;
     int i=0;
     //printf("block%d\n",blocks);
     for(i=0; i<blocks; i++)
     {
        struct cache *cache = cache_block (inode->data.ind, WRITE_BLOCK);
        struct index *index = calloc(1,sizeof *index);
        ASSERT(cache->io.holder!=thread_current());
        write_cache(cache);
        memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     //printf("<6>\n");
        free_map_allocate(1,&index->block[block_current+i-1]);
     //printf("<7>\n");
        memcpy(&cache->data,index,BLOCK_SECTOR_SIZE);
        end_write(cache);
        cache = cache_block (index->block[block_current+i-1], WRITE_BLOCK);
        write_cache(cache);
        memset((uint8_t *)&cache->data,0,BLOCK_SECTOR_SIZE);
        end_write(cache);
        free(index);
     }
     return block;
  }
  if(length<129*512)
  {
     struct cache *cache = cache_block (inode->data.ind, READ_BLOCK);
     struct index *index = calloc(1,sizeof *index);
     read_cache(cache);
     memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     if(length% BLOCK_SECTOR_SIZE!=0)
     {
        cache = cache_block (index->block[block_current-1], WRITE_BLOCK);
        write_cache(cache);
        memset((uint8_t *)&cache->data+length % BLOCK_SECTOR_SIZE,0,512-length % BLOCK_SECTOR_SIZE);
        end_write(cache);
        free(index);
        block_current++;
     }
     int blocks= 129- block_current;
     int i=0;
     for(i=0; i<blocks; i++)
     {
        struct cache *cache = cache_block (inode->data.ind, WRITE_BLOCK);
        struct index *index = calloc(1,sizeof *index);
        write_cache(cache);
        memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
        free_map_allocate(1,&index->block[block_current+i-1]);
        memcpy(&cache->data,index,BLOCK_SECTOR_SIZE);
        end_write(cache);
        cache = cache_block (index->block[block_current+i-1], WRITE_BLOCK);
        write_cache(cache);
        memset((uint8_t *)&cache->data,0,BLOCK_SECTOR_SIZE);
        end_write(cache);
        free(index);
     }
     block_current = 129;
     length = 129*BLOCK_SECTOR_SIZE;
  }
  if(block==129 && block_ofs==0)
     return block;
  if(length==512*129)
     free_map_allocate(1,&inode->data.din);
  if( length % BLOCK_SECTOR_SIZE !=0)
  {
     int i= (block_current-129)/128;
     int j= (block_current-129)%128;
     struct cache *cache = cache_block (inode->data.din, READ_BLOCK);
     struct index *index = calloc(1,sizeof *index);
     read_cache(cache);
     memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     cache = cache_block (index->block[i], READ_BLOCK);
     read_cache(cache);
     memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     cache = cache_block (index->block[j], WRITE_BLOCK);
     write_cache(cache);
     memset((uint8_t *)&cache->data+length % BLOCK_SECTOR_SIZE,0,512-length % BLOCK_SECTOR_SIZE);
     end_write(cache);
     block_current++;
     free(index);
  }
  int mi=(block-129)/128;
  int mj=(block_current-129)/128;
  int ni=(block-129) % 128;
  int nj=(block_current-129)%128;
  if(nj==0)
  {
     struct cache *cache = cache_block (inode->data.din, WRITE_BLOCK);
     struct index *index = calloc(1,sizeof *index);
     write_cache(cache);
     memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     free_map_allocate(1,&index->block[mj]);
     memcpy(&cache->data,index,BLOCK_SECTOR_SIZE);
     end_write(cache);
     free(index);
  }
  int k=0;
  for(k=0;k<mj-mi;k++)
  {
     struct cache *cache = cache_block (inode->data.din, WRITE_BLOCK);
     struct index *index = calloc(1,sizeof *index);
     write_cache(cache);
     memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
     free_map_allocate(1,&index->block[mj+k+1]);
     memcpy(&cache->data,index,BLOCK_SECTOR_SIZE);
     end_write(cache);
     free(index);
  }
  int i,j=0;
  for(i=mj;i<mi+1;i++)
  {
     int low = (i==mj?nj:0);
     int high = (i==mi?ni+1:128);
     for(j=low;j<high;j++)
     {   
         struct cache *cache = cache_block (inode->data.din, READ_BLOCK);
         struct index *index = calloc(1,sizeof *index);
         read_cache(cache);
         memcpy(index, &cache->data,BLOCK_SECTOR_SIZE); 
         end_read(cache);
         cache = cache_block (index->block[i], WRITE_BLOCK);
         write_cache(cache);
         memcpy(index, &cache->data,BLOCK_SECTOR_SIZE);
         free_map_allocate(1,&index->block[j]);
         memcpy(&cache->data,index,BLOCK_SECTOR_SIZE);
         end_write(cache);
         cache = cache_block (index->block[j], WRITE_BLOCK);
         write_cache(cache);
         memset((uint8_t *)&cache->data,0,BLOCK_SECTOR_SIZE);
         end_write(cache);
         free(index);
     }
  }
  return block;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  //printf("get lock:%d\n",pos);
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
  {
     int block= pos/BLOCK_SECTOR_SIZE;
     if(block<1)
        return inode->data.sp;
     if(block<129)
     {
        //printf("%d:get lock\n",block);
        //printf("%d\n",inode->data.ind);
        struct cache *cache=cache_block(inode->data.ind,READ_BLOCK);
        //printf("%d:get cache\n",block);
        struct index *index=calloc(1,sizeof *index);
        //printf("%d:get index\n",block);
        read_cache(cache);
        memcpy(index,&cache->data,BLOCK_SECTOR_SIZE);
        //printf("memcpy\n");
        end_read(cache);
        block_sector_t b=index->block[block-1];
        free(index);
        return b;
     }
     struct cache *cache=cache_block(inode->data.din,READ_BLOCK);
     struct index *index=calloc(1,sizeof *index);
     read_cache(cache);
     memcpy(index,&cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     int i =(block-129)/128;
     int j =(block-129)%128;
     cache=cache_block(index->block[i],READ_BLOCK);
     read_cache(cache);
     memcpy(index,&cache->data,BLOCK_SECTOR_SIZE);
     end_read(cache);
     block_sector_t b=index->block[j];
     free(index);
     //printf("block:%d\n",b);
     return b;
  }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&inode_lock);
}

bool inode_alloc(struct inode_disk *disk_inode)
{
   struct inode *inode=malloc(sizeof(struct inode));
   memcpy(&inode->data,disk_inode,BLOCK_SECTOR_SIZE);
   inode->data.length=0;
   file_growth(inode,0,disk_inode->length);
   memcpy(disk_inode,&inode->data,BLOCK_SECTOR_SIZE);
   free(inode);
   return true;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

bool
inode_create (block_sector_t sector, off_t length,bool isdir)
{
  lock_acquire(&file_lock);
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  //printf("here\n");
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->sp=NULL;
      disk_inode->ind=NULL;
      disk_inode->din=NULL;
      disk_inode->isdir=isdir;
      success=inode_alloc(disk_inode);
      disk_inode->parent = ROOT_DIR_SECTOR;
      struct cache *cache=cache_block(sector,WRITE_BLOCK);
      write_cache(cache);
      memcpy(&cache->data,disk_inode,BLOCK_SECTOR_SIZE);
      end_write(cache);
      free (disk_inode);
    }
  //printf("inode create end\n");
  lock_release(&file_lock);
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
find_inode(block_sector_t sector)
{
  lock_acquire(&file_lock);
  struct list_elem *e;
  struct inode *inode;
  //printf("here\n");
  //printf("%u\n",inode->data);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          lock_release(&file_lock);
          return inode; 
        }
    }
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    lock_release(&file_lock);
    return NULL;
  }
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init(&inode->io);
  cond_init(&inode->read_end);
  lock_acquire(&inode->io);
  inode->sector = sector;
  inode->open_cnt = 0;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->read_cnt=0;
  struct cache *cache=cache_block(inode->sector, READ_BLOCK);
  write_cache(cache);
  memcpy(&inode->data,&cache->data,BLOCK_SECTOR_SIZE);
  end_write(cache);
  lock_release(&inode->io);
  lock_release(&file_lock);
  return inode;
}
struct inode *
inode_open (block_sector_t sector)
{
  lock_acquire(&file_lock);
  struct list_elem *e;
  struct inode *inode;
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          lock_release(&file_lock);
          return inode; 
        }
    }
  //printf("<2>\n");
  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    lock_release(&file_lock);
    return NULL;
  }
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_init(&inode->io);
  cond_init(&inode->read_end);
  lock_acquire(&inode->io);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->read_cnt=0;
  struct cache *cache=cache_block(inode->sector, READ_BLOCK);
  write_cache(cache);
  memcpy(&inode->data,&cache->data,BLOCK_SECTOR_SIZE);
  end_write(cache);
  lock_release(&inode->io);
  //printf("inode open end\n");
  lock_release(&file_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
  {
    lock_acquire(&inode->io);
    inode->open_cnt++;
    lock_release(&inode->io);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  lock_acquire(&inode->io);
  block_sector_t ret=inode->sector;
  lock_release(&inode->io);
  return ret;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  int fl=0;
  if(file_lock.holder!=thread_current())
  {
     fl=1;
     lock_acquire(&file_lock);
  }
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
   
      /* Deallocate blocks if removed. */
      struct cache *cache = cache_block(inode->sector,WRITE_BLOCK);
      write_cache(cache);
      memcpy(&cache->data,&inode->data,BLOCK_SECTOR_SIZE);
      end_write(cache);
      if (inode->removed) 
        {
          inode_delete(inode);
          free_map_release (inode->sector, 1);
          struct cache *cache = find_cache(inode->sector);
          if(cache)
          {
             cache_remove(cache);
          }
        }          
      free (inode); 
    }
  if(fl==1)
  {
     lock_release(&file_lock);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  lock_acquire(&inode->io);
  ASSERT (inode != NULL);
  inode->removed = true;
  lock_release(&inode->io);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  read_inode(inode);
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  if(offset>inode->data.length)
  {
     read_end(inode);
     return 0;
  }
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      //printf("read:%d,%d\n",sector_idx,offset);
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode->data.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache *c=cache_block(sector_idx, READ_BLOCK);
      read_cache(c);
      memcpy (buffer + bytes_read, (uint8_t *)&c->data + sector_ofs, chunk_size);
      end_read(c);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  read_end(inode);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  int wi=0;
  if(inode->io.holder!=thread_current())
  {
     wi=1;
     write_inode(inode);
  }
  //printf("here5\n");
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  if (inode->deny_write_cnt)
  {
    if(wi==1)
       write_end(inode);
    return 0;
  }
  //printf("before growth\n");
  if(offset+size>inode->data.length)
  {
    file_growth(inode,offset,size);
  }
  //printf("len:%d\n",inode->data.length);
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left =inode->data.length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      /*
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }
      */
      //printf("here\n");
      struct cache *c=cache_block(sector_idx,WRITE_BLOCK);
      //printf("get cache\n");
      write_cache(c);
      memcpy ((uint8_t *)&c->data + sector_ofs,buffer + bytes_written, chunk_size);
      end_write(c);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  //printf("inode write end\n");
  if(wi==1)
     write_end(inode);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  lock_acquire(&inode->io);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->io);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  lock_acquire(&inode->io);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release(&inode->io);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  lock_acquire(&inode->io);
  int ret=inode->data.length;
  lock_release(&inode->io);
  return ret;
}
void inode_close_all()
{
  struct list_elem *e;
  struct inode *inode=NULL;
  for(e=list_begin(&open_inodes);e!=list_end(&open_inodes);)
  {
     struct list_elem *next=list_next(e);
     inode=list_entry(e,struct inode,elem);
     if(inode_isdir(inode)&&inode->sector!=0)
     {
        inode->open_cnt=1;
        inode_close(inode);
     }
     e=next;
  }
}
