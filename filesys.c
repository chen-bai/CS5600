#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  cache_system_init();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  inode_close_all();
  free_map_close ();
  free_cache();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size,bool isdir) 
{
  if (strlen (name) == 0)
    return false;
  block_sector_t inode_sector = 0;
  struct name_in_dir *nid;
  nid=true_dir(name);
  if(nid==NULL)
     return false;
  //printf("create:%s\n",name);
  struct dir *dir =nid->dir;
  char *exec_name=nid->name;
  //printf("create:%s\n",exec_name);
  //printf("create:%d\n",isdir);
  bool success=false;
  if (strcmp (exec_name, ".") != 0 && strcmp (exec_name, "..") != 0)
    {
      success = (dir != NULL && free_map_allocate (1, &inode_sector)
	  && inode_create (inode_sector, initial_size, isdir)
	  && dir_add (dir, exec_name, inode_sector));
      if (!success && inode_sector != 0)
	free_map_release (inode_sector, 1);
      if (dir != NULL && isdir)
	{
	  struct inode *inode=inode_open(inode_sector); 
          if(inode==NULL)
          {
             dir_close (dir);
             free(exec_name);
             free(nid);
             return false;  
          }
          //printf("%s,%d\n",exec_name,inode->sector);    
          inode_set_parent(inode,inode_get_inumber(dir_get_inode (dir)));
          inode_close(inode);
	}
    }
  dir_close (dir);
  free(exec_name);
  //printf("create_end:%d\n",success);
  free(nid);
  return success;
}


/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  //printf("here\n");
  if (strlen (name) == 0)
    return NULL;
  block_sector_t inode_sector = 0;
  struct name_in_dir *nid=true_dir(name);
  if(nid==NULL)
     return NULL;
  struct dir *dir =nid->dir;
  char *exec_name=nid->name;
  struct inode *inode = NULL;
  if (dir != NULL)
    {
      if (exec_name == NULL || strcmp (exec_name, ".") == 0)
	{
            free (exec_name);
            free(nid);
            struct file* file = (struct file*) dir;
            file->deny_write = false;
            return file;
	}
      else if (strcmp (exec_name, "..") == 0)
	{
          if(inode_parent(dir_get_inode (dir))==0)
          {
             dir_close (dir);
	     free (exec_name);
             free(nid);
             return NULL;
          }
	  inode = inode_open (inode_parent(dir_get_inode (dir)));
	}
      else
	{
	  dir_lookup (dir, exec_name, &inode);
	}
    }
  dir_close (dir);
  free (exec_name);
  free(nid);
  if(!inode)
     return NULL;
  if(inode_isdir(inode))
  {
      struct file* file = (struct file*) dir_open(inode);
      file->deny_write = false;
      return file;
  }
  else
      return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (strlen (name) == 0)
    return false;
  block_sector_t inode_sector = 0;
  struct name_in_dir *nid=true_dir(name);
  if(nid==NULL)
     return false;
  struct dir *dir =nid->dir;
  char *exec_name=nid->name;
  if (exec_name == NULL)
    {
      dir_close (dir);
      free (exec_name);
      free (nid);
      return false;
    }
  bool success = dir != NULL && dir_remove (dir, exec_name);
  dir_close (dir);
  free (exec_name);
  free (nid);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  //printf("here\n");
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
