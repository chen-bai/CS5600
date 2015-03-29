#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "lib/user/syscall.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/suppage.h"
#include "vm/swap.h"
#include "threads/pte.h"

bool check_esp(unsigned esp){
   if(esp<=0x08048000||esp>PHYS_BASE)
      return false;
   return true;
}
static thread_func start_process NO_RETURN;
char * determine_argument(char *file_name)
{
  char *argument;
  char *execute_file_name = malloc((strlen(file_name)+1)*sizeof(char));
  memcpy(execute_file_name, file_name, strlen(file_name)+1 );
  execute_file_name=strtok_r((char *) execute_file_name, " ", &argument);
  return execute_file_name;
}
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
void child_init(struct child_process_elem *child)
{
   child->process_wait=false;
   child->exit=false;
   child->next_fd=2;
   list_init(&child->file_list);
   return;
}
struct child_process_elem *create_child(pid_t pid)
{
   struct child_process_elem *child=malloc(sizeof(struct child_process_elem));
   struct thread *cur=thread_current();
   child_init(child);
   child->pid=pid;
   child->load=false;
   sema_init(&child->wait_barrier,0);
   sema_init(&child->load_barrier,0);
   list_push_back(&cur->child_process_list,&child->elem);
   return child;
}
struct child_process_elem *get_child(pid_t pid)
{
    struct thread *cur=thread_current();
    struct list_elem *e;
    //printf("<2>\n");
    if(!list_empty(&cur->child_process_list))
    {
       //printf("<0>\n");
       for(e=list_begin(&cur->child_process_list);e!=list_end(&cur->child_process_list);e=list_next(e))
       {
          //printf("<1>\n");
          struct child_process_elem *child=list_entry(e,struct child_process_elem,elem);
          if(child->pid==pid)
             return child;
       }
    }
    //printf("NULL\n");
    return NULL;
}
void remove_child(struct child_process_elem *child)
{
   //printf("<5>\n");
   list_remove(&child->elem);
   //printf("<4>\n");
   free(child);
   return;
}
void
wait_return(struct child_process_elem *child)
{
   //printf("<1>\n");
   sema_down(&child->wait_barrier);
   //printf("%d\n",child->status);
   return;
}
tid_t
process_execute (const char *file_name) 
{
  //printf("process execute:%s\n",file_name);
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  //char *argument;
  strlcpy (fn_copy, file_name, PGSIZE);
  //file_name=strtok_r((char *) file_name, " ", &argument);
  char *execute_file_name=determine_argument(file_name);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (execute_file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 
  //printf("process execute end\n");
  free(execute_file_name);
  return tid;
}


/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  //printf("start process\n");
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  //char *argument; 
  list_init(&thread_current()->page_table);
  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  
  success = load (file_name, &if_.eip, &if_.esp);
  /* If load failed, quit. */
  //printf("<?>\n");
  palloc_free_page (file_name);
  
  if (!success) 
     exit(-1);
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  //printf("<?>\n");
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
  
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  //printf("process wait:%d\n",child_tid);
  struct child_process_elem *child=get_child(child_tid);
  //ASSERT(child);
  if(!child||child->process_wait)
     return -1;
  child->process_wait=true;
  wait_return(child);
  //printf("<***>\n");
  if(!child->exit)
     child->status=-1;
  int ret_status=child->status;
  remove_child(child);
  //printf("pid:%d\n",child->status);
  //printf("wait end\n");
  return ret_status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  //printf("process exit\n");
  struct thread *cur = thread_current ();
  uint32_t *pd;
  //ASSERT(1==0);  
  munmap_all();
  while(!list_empty(&cur->page_table))
  {
     struct list_elem *e=list_begin(&cur->page_table);
     struct sup_page_table *spt=list_entry(e,struct sup_page_table,elem);
     struct frame_elem *frame=spt->frame;
     spt_free(spt);
     frame_free(frame);
  }
   while(!list_empty(&cur->child_process_list))
   {
      //printf("efg\n");
      struct list_elem *e=list_pop_front(&cur->child_process_list);
      free(list_entry(e,struct child_process_elem,elem));
      //printf("efg end\n");
   }
  file_close(cur->file); 
  if(cur->process)
  {
     //printf("<3>\n");
     sema_up(&thread_current()->process->wait_barrier);
     close_all();
     //remove_child(cur->process);
  }  
  //printf("<4>\n");
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp,char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  //printf("load\n");
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  //printf("%s\n",file_name);
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();
  char *execute_file_name=determine_argument(file_name);
  /* Open executable file. */
  lock_acquire(&file_lock);
  file = filesys_open (execute_file_name); 
  //printf("%u\n",file);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", execute_file_name);
      goto done; 
    }
  else
    file_deny_write (file);
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", execute_file_name);
      goto done; 
    }
  free(execute_file_name);
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
              {   
   		//printf("load_segment fail!!\n");
                goto done;
              }
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  //printf("'%s'\n",file_name);
  if (!setup_stack (esp, file_name))
    goto done;

  /* Start address. */
  //printf("<0>\n");
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  //file_close (file);
  //printf("load end: %d\n",success);
  thread_current()->file=file;
  thread_current()->process->load=success;
  lock_release(&file_lock);
  sema_up(&thread_current()->process->load_barrier);
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
  //lock_acquire(&file_lock);
  file_seek (file, ofs);
  //int i=0;
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
   
      /*Get a page of memory. */
      /*
      uint8_t *kpage = frame_get_kpage(PAL_USER);
      if (kpage == NULL)
      {
        evict();
        kpage = frame_get_kpage(PAL_USER);
      }
      
      /* Load this page. */
      /*
      if (file_read_at(file, kpage, page_read_bytes,ofs) != (int) page_read_bytes)
        {
          palloc_free_page(kpage);
          return false; 
        }
      
      //printf("<2>\n");
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      /*
      if(!install_frame (upage, kpage, writable,file,SWAP,ofs,page_read_bytes)) 
      {
          frame_destroy(kpage);
          return false; 
      }
      */
      install_frame (upage, NULL, writable,file,FILE,ofs,page_read_bytes);
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;     
    }
  //lock_release(&file_lock);
  //printf("<%d>\n",++i);
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp,char *file_name) 
{
  uint8_t *kpage;
  bool success = false;
  //printf("<1>\n");
  kpage = palloc_get_page(PAL_USER | PAL_ZERO);
  //kpage=frame_get_kpage (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      //printf("here\n");
      success = install_frame (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true,NULL,SWAP,NULL,NULL);
      if (success)
      {  
         *esp = PHYS_BASE;
         thread_current()->esp=((uint8_t *) PHYS_BASE) - PGSIZE;
      }
      else
      {
         frame_destroy(kpage);
         return success;
      }
    }
   else
   {
      while(!kpage)
      {
         evict();
         //printf("z\n");
         kpage=palloc_get_page(PAL_USER | PAL_ZERO);
      }
      success = install_frame (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true,NULL,SWAP,NULL,NULL);
      if (success)
      {  
         *esp = PHYS_BASE;
         thread_current()->esp=((uint8_t *) PHYS_BASE) - PGSIZE;
      }
      else
      {
         frame_destroy(kpage);
         return success;
      }
   }
   char *token, *save_ptr;
   int size =64 ;
   char **argv[size];
   int argc=0;
   //printf("'%s'\n",file_name);
   //printf("<start>\n");
   //struct list argv_list;
   //list_init(&argv_list);
   for (token = strtok_r ((char *)file_name, " ", &save_ptr); token != NULL;
          token = strtok_r (NULL, " ", &save_ptr))
   {
       if(!check_esp(*esp-(strlen(token)+1)))
          return false;
       *esp-=(strlen(token)+1);
       argv[argc] = *esp;
       //struct argv_elem *e;
       //e->argv=*esp;
       //list_push_back(&argv_list,&e->elem);
       //printf("%d\n",argc);
       memcpy(*esp, token, strlen(token) + 1);
       argc++;
       if(argc>=size)
       {        
          /*
          char **argv_cpy[size];
          int j;
          for(j=0;j<size;j++)
             argv_cpy[j]=argv[j];
          char **argv[2*size];
          for(j=0;j<size;j++)
             argv[j]=argv_cpy[j];
          size*=2;       
          */
          //printf("there are too many argument to be executed!\n");
          return false;   
       }
   }
   /* char **argv[argc+1];
   int k=0;
   struct list_elem *cpy=list_begin(&argv_list);
    for(k=0; k<argc; k++)
   {
      argv[k]=list_entry(cpy,struct argv_elem,elem)->argv;
      cpy=list_next(cpy);
   } */
   //printf("<end>\n");
   if(!check_esp(*esp-sizeof(char **)-sizeof(uint8_t)-argc*sizeof(char *)-sizeof(char **)-sizeof(int)-sizeof(void *)))
      return false;
   argv[argc]=0;
   if ((size_t) *esp % 4)
    {
      *esp -=(size_t) *esp % 4;
      size_t *word= 0;
      memcpy(*esp, &word, (size_t) *esp % 4);
    }
   int i=0;
   for (i = 0; i <= argc; i++)
   {
      *esp -= sizeof(char *);
      memcpy(*esp, &argv[argc-i], sizeof(char *));
   }
   //printf("%d\n",argc);
   char *esp_cpy=*esp;
   *esp -= sizeof(char **);
   memcpy(*esp,&esp_cpy, sizeof(char **));
   *esp -= sizeof(int);
   memcpy(*esp, &argc, sizeof(int));
   *esp -= sizeof(void *);
   void *fake = 0;
   memcpy(*esp, &fake, sizeof(void *));
   return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool install_file(void *upage, void *kpage, struct file *file, enum page_type type,off_t ofs,size_t page_read_bytes, bool writable)
{
   //printf("<231212>\n");
   struct frame_elem *match=check_match(file,ofs,page_read_bytes);
   //printf("<3>\n");
   if(match!=NULL&&(type==FILE||type==MMAP))
   {  
     // printf("a\n");
      printf("uupage:%d\n",upage);
      if(!match->shared)
      {
         printf("a\n");
         struct shared_frame *sf=malloc(sizeof(struct shared_frame));
         list_init(&sf->spt_list);
         match->shared=sf;
         match->writable=false;
         pagedir_set_writable(match->spt->thread->pagedir,match->kpage,false);
      }
      printf("b\n");
      struct sup_page_table *spt=spt_init(upage,match);
      list_push_back(&match->shared,&spt->felem);
      bool success=install_page(spt->upage,match->kpage,false);
      return success;
   }
   struct frame_elem *frame=frame_table_insert(NULL,file,type,ofs,page_read_bytes,writable);
   struct sup_page_table *spt=spt_init(upage,frame);
   //printf("upage:%d\n",spt->upage);
   pagedir_clear_page (spt->thread->pagedir, spt->upage);
   return true;
}
bool
install_frame(void *upage, void *kpage, bool writable, struct file *file, enum page_type type,off_t ofs,size_t page_read_bytes)
{
  struct thread *t = thread_current ();
  ASSERT(pg_round_down(upage)==upage);
  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  //struct sup_page_table *spt=spt_init(upage);
  if(type==FILE||type==MMAP)
  {
     //printf("install for:%u\n",type);
     return install_file(upage,NULL,file,type,ofs,page_read_bytes,writable);
  }
  struct frame_elem *frame=frame_table_insert(kpage,file,type,ofs,page_read_bytes,writable);
  struct sup_page_table *spt=spt_init(upage,frame);
  //printf("STACK:%u\n",spt->upage);
  return install_page(spt->upage,frame->kpage,writable);
}
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
