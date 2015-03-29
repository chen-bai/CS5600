#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "lib/user/syscall.h"
struct child_process_elem{
pid_t pid;
int load;
bool process_wait;
bool exit;
bool die;
int status;
struct list_elem elem;
struct list file_list;
int next_fd;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);


#endif /* userprog/process.h */

