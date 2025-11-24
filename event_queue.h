#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <unistd.h>
#include <time.h>

//Every time you add a new (non-main) override in define_override.c, make an ID for it here
enum OVERRIDE_ID {
    MALLOC,
    CALLOC,
    FREE,
    THREAD_CREATE,
    THREAD_EXIT,
    EXIT,
    FORK,
    REALLOC,
    MMAP,
    MUNMAP,
    STRNCPY,
    MEMCPY,
    CLONE3,
    MAX_OVERRIDE_VAL //Not an actual override, just easy way to get size of enum.
};


typedef struct mmap_data {
    void *addr;
    size_t len; 
    int prot; 
    int flags; 
    int fd; 
    long offset;
    void* retVal;
} mmap_data;

void push_event(int event_type, void* data, struct timespec* buffer);

void end_loop(void);
void restart_loop(void);

#endif /* EVENT_QUEUE_H */







