
#define _GNU_SOURCE
#include "event_queue.h"
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

typedef struct event {
    struct timespec time;
    void* data;
    int event_type;
    pid_t thread_id;
    struct event* next;
} event;

static event* first;
static event* last;
static int size;

static pthread_mutex_t lock;
static pthread_cond_t cond;

static FILE* files[MAX_OVERRIDE_VAL];

static int keep_looping;

//Printed timestamps are only relative to the very first event (just before main() starts)
static unsigned long origin = 0;


static inline void pp(void* ptr, FILE* f, int newline) {
    if (ptr == NULL) fprintf(f, "null");
    else fprintf(f, "\"%p\"", ptr);
    fprintf(f,newline ? "\n" : ",");
}

static void handle_malloc(void* info, FILE* f) {
    size_t* data = info;
    //Size, then return value
    fprintf(f, "%lu,", data[0]);
    pp((void*)data[1], f, 1);
}

static void handle_calloc(void* info, FILE* f) {
    size_t* data = info;
    size_t total = data[0] * data[1];
    fprintf(f, "%lu,%lu,%lu,", data[0], data[1], total);
    pp((void*)data[2], f, 1);
}

static void handle_free(void* info, FILE* f) {
    pp(info, f, 1);
}

static void handle_pthread_create(void* info, FILE* f) {
    void** data = info;

    pp(data[0], f, 0);
    pp(data[1], f, 1);
}

static void handle_pthread_exit(void* info, FILE* f) {
    pp(info, f, 1);
}

static void handle_exit(void* info, FILE* f) {
    fprintf(f, "%ld\n", (long)info);
}

static void handle_fork(void* info, FILE* f) {
    fprintf(f, "%ld\n", (long)info);
}

static void handle_realloc(void* info, FILE* f) {
    void** data = info;
    pp(data[0], f, 0);
    fprintf(f, "%lu,", (size_t)data[1]);
    pp(data[2], f, 1);
}

static inline void pb(int bool, FILE* f, int newline) {
    fprintf(f, bool ? "True":"False");
    fprintf(f,newline ? "\n" : ",");
}

static void handle_mmap(void* info, FILE* f) {
    mmap_data* data = info;

    pp(data->addr, f, 0);
    fprintf(f, "%lu,", data->len);

    int anyPerms = 0;
    int prots[] = {PROT_EXEC, PROT_READ, PROT_WRITE};
    for (int i = 0; i < 3; i++) {
        int b = data->prot & prots[i];
        if (b) anyPerms = 1;
        pb(b, f, 0);
    }
    pb(!anyPerms, f, 0);


    int flags[] = {MAP_SHARED, MAP_PRIVATE, MAP_32BIT, MAP_ANON, MAP_FIXED, MAP_FIXED_NOREPLACE, MAP_GROWSDOWN, MAP_HUGETLB,
                    MAP_LOCKED, MAP_NONBLOCK, MAP_NORESERVE, MAP_POPULATE, MAP_SYNC};

    for (int i = 0; i < 13; i++) {
        pb(data->flags & flags[i], f, 0);
    }

    fprintf(f, "%d,%ld,", data->fd, data->offset);
    pp(data->retVal, f, 1);
}

static void handle_munmap(void* info, FILE* f) {
    void** data = info;

    pp(data[0], f, 0);
    fprintf(f, "%lu,", (size_t)data[1]);
    pb(data[2] == NULL, f, 1);
}

void push_event(int event_type, void* data) {
    struct timespec time;
    timespec_get(&time, TIME_UTC);

    event* e = malloc(sizeof(event));
    if (e == NULL) {
        fprintf(stderr, "Unable to allocate event\n");
        exit(1);
    }

    e->event_type = event_type;
    e->data = data;
    e->next = NULL;
    e->thread_id = gettid();
    e->time = time;

    pthread_mutex_lock(&lock);

    if (origin == 0) {
        origin = (time.tv_sec * 1000000000L) + time.tv_nsec;
        char print[100];
        sprintf(print, "%lu", origin);
        setenv("LD_ORIGIN_TIME", print,1);
    }

    if (first == NULL) {
        first = e;
    }
    if (last != NULL) {
        last->next = e;
    }
    last = e;

    if (++size >= 20) {
        pthread_cond_signal(&cond);
    }

    pthread_mutex_unlock(&lock);
}


int create_file(int event_type, FILE** file) {
    if (files[event_type] != NULL) {
        *file = files[event_type];
        return 0;
    }
    char* log_root = getenv("LD_PRELOAD_LOG");
    if (log_root == NULL || log_root[0] == '\0') log_root = "./logs/";


    pid_t pid = getpid();
    const char* event_names[] = { "malloc", "calloc", "free", "thread_create", "thread_exit", "exit", "fork", "realloc", "mmap", "munmap" };
    char path[4096];

    snprintf(path, sizeof(path), "%s/%d/%s.csv", log_root, pid, event_names[event_type]);
    // Create directory if needed
    char dir_path[4096];
    snprintf(dir_path, sizeof(dir_path), "%s/%d", log_root, pid);
    mkdir(log_root, 0777);
    mkdir(dir_path, 0777);

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        fprintf(stderr, "FAILED TO CREATE LOG FILE: %s\n", path);
        exit(1);
    }
    int fd = fileno(f);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    files[event_type] = f;
    *file = f;
    return 1;
}

void flush_events(void) {
    event* e;

    pthread_mutex_lock(&lock);

    if (keep_looping && first == NULL) pthread_cond_wait(&cond, &lock);
    
    e = first;
    first = NULL;
    last = NULL;
    size = 0;

    pthread_mutex_unlock(&lock);

    while (e != NULL) {

        int type = e->event_type;

        FILE* f;
        int print_first_line = create_file(type, &f);

        if (print_first_line) {
            char* line;

            switch(type) {
                case MALLOC:
                    line = "size,return_value";
                    break;
                case CALLOC:
                    line = "members,size_per_member,total_size,return_value";
                    break;
                case FREE:
                    line = "address";
                    break;
                case THREAD_CREATE:
                    line = "function,arg";
                    break;
                case THREAD_EXIT:
                    line = "return_value";
                    break;
                case EXIT:
                    line = "code";
                    break;
                case FORK:
                    line = "return_value";
                    break;
                case REALLOC:
                    line = "original_pointer,new_size,return_value";
                    break;
                case MMAP:
                    line = "hint_address,size,executable,readable,writable,inaccessible,shared,copy_on_write,32_bit,anonymous,exact_hint,no_replace,grows_down,huge_page,locked,no_blocking,no_reserve,populate,sync,file_desc,offset,return_value";
                    break;
                case MUNMAP:
                    line = "address,size,success";
                    break;
                default:
                    fprintf(stderr, "UNHANDLED EVENT SPOTTED\n");
                    exit(1);
            }

            fprintf(f, "thread,time_ns,%s\n", line);
        }

        // Convert to REALTIVE nanoseconds since epoch for reasonable JSON numbers
        unsigned long time_ms = ((e->time.tv_sec * 1000000000L) + e->time.tv_nsec) - origin;

        //All file lines start with a thread id and timestamp
        fprintf(f, "%d,%ld,", e->thread_id, time_ms);

        int should_free_data = 1;
        switch(type) {
            case MALLOC:
                handle_malloc(e->data, f);
                break;
            case CALLOC:
                handle_calloc(e->data, f);
                break;
            case FREE:
                handle_free(e->data, f);
                should_free_data = 0; // FREE stores a pointer value, not allocated data
                break;
            case THREAD_CREATE:
                handle_pthread_create(e->data, f);
                break;
            case THREAD_EXIT:
                handle_pthread_exit(e->data, f);
                should_free_data = 0; // THREAD_EXIT stores a pointer value, not allocated data
                break;
            case EXIT:
                handle_exit(e->data, f);
                should_free_data = 0; // EXIT stores an integer value, not allocated data
                break;
            case FORK:
                handle_fork(e->data, f); //Fork shows a process id, not allocated data
                should_free_data = 0;
                break;
            case REALLOC:
                handle_realloc(e->data, f);
                break;
            case MMAP:
                handle_mmap(e->data, f);
                break;
            case MUNMAP:
                handle_munmap(e->data, f);
                break;
            default:
                fprintf(stderr, "UNHANDLED EVENT SPOTTED\n");
                exit(1);
        }

        event* next = e->next;

        if (should_free_data) {
            free(e->data);
        }
        free(e);
        e = next;
    }
}

static void* thread_loop(void* arg) {
    while (keep_looping) {
        flush_events();
    }
    return NULL;
}


pthread_t thread;

void end_loop(void) {
    pthread_mutex_lock(&lock);
    keep_looping = 0;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);

    pthread_join(thread, NULL);
}

void restart_loop(void) {
    keep_looping = 1;
    pthread_create(&thread, NULL, thread_loop, NULL);
}

__attribute__((constructor))
void init(void) {
    //This environment variable is so descendants from forks record their time relative to the start of the original "root" process.
    //Don't set this yourself.
    char* originStr = getenv("LD_ORIGIN_TIME");
    if (originStr == NULL || originStr[0] == '\0') {
        origin = 0;
    }
    else {
        origin = strtoul(originStr, NULL, 10);
    }

    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    first = NULL;
    last = NULL;
    
    restart_loop();

    //Children don't copy threads, and are likely to exec() to something else, restarting their own setup anyway.
    //To be on safe side, worker thread should be shut down during fork, and only start back up for parent.
    pthread_atfork(end_loop, restart_loop, NULL);
}

__attribute__((destructor))
void fini(void) {
    //Behavior here runs whenn the library unloads, after execution is over.
    end_loop();

    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    for (int i = 0; i < MAX_OVERRIDE_VAL; i++) {
        if (files[i]) fclose(files[i]);
    }
}
