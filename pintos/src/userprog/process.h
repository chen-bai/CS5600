#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "threads/synch.h"
struct child_process_elem{
pid_t pid;
bool process_wait;
bool exit;
bool load;
int status;
struct list_elem elem;
struct list file_list;
struct semaphore load_barrier;
struct semaphore wait_barrier;
int next_fd;
};
struct child_process_elem *get_child(pid_t pid);
struct child_process_elem *create_child(pid_t pid);
void child_init(struct child_process_elem *child);
void remove_child(struct child_process_elem *child);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *upage, void *kpage, bool writable);
bool install_frame(void *upage, void *kpage, bool writable, struct file *file, enum page_type type,off_t ofs,size_t page_read_bytes);


#endif /* userprog/process.h */

