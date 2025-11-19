
#define _GNU_SOURCE
#include "event_queue.h"
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
static FILE* file;

static int keep_looping;

//Printed timestamps are only relative to the very first event (just before main() starts)
static unsigned long origin = 0;

static void handle_malloc(void* info) {
    size_t* data = info;
    fprintf(file, "\"type\": \"malloc\", \"size\": %ld, \"returnValue\": ", data[0]);
    if (data[1] == 0) fprintf(file, "null}");
    else fprintf(file, "\"%p\"}", (void*)data[1]);
}

static void handle_calloc(void* info) {
    size_t* data = info;
    size_t total = data[0] * data[1];
    fprintf(file, "\"type\": \"calloc\", \"size\": %ld, \"members\": %ld, \"sizePerMember\": %ld, \"returnValue\": ", total, data[0], data[1]);
    if (data[2] == 0) fprintf(file, "null}");
    else fprintf(file, "\"%p\"}", (void*)data[2]);
}

static void handle_free(void* info) {
    fprintf(file, "\"type\": \"free\", \"address\": ");
    
    if (info == NULL) fprintf(file, "null}");
    else fprintf(file, "\"%p\"}", info);
}

static void handle_pthread_create(void* info) {
    void** data = info;
    fprintf(file, "\"type\": \"thread_start\", \"function\": ");

    if (data[0] == NULL) fprintf(file, "null, \"arg\": ");
    else fprintf(file, "\"%p\", \"arg\": ", data[0]);

    if (data[1] == NULL) fprintf(file, "null}");
    else fprintf(file, "\"%p\"}",data[1]);
}

static void handle_pthread_exit(void* info) {
    fprintf(file, "\"type\": \"thread_end\", \"returnValue\": ");
    if (info == NULL) fprintf(file, "null}");
    else fprintf(file, "\"%p\"}", info);
}

static void handle_exit(void* info) {
    fprintf(file, "\"type\": \"exit\", \"code\": %ld}", (long)info);
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

    if (origin == 0) origin = (time.tv_sec * 1000000000L) + time.tv_nsec;
    

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

void flush_events(int* first_time) {
    event* f;

    pthread_mutex_lock(&lock);

    if (keep_looping && first == NULL) pthread_cond_wait(&cond, &lock);
    
    f = first;
    first = NULL;
    last = NULL;
    size = 0;
    pthread_mutex_unlock(&lock);

    int did_something = 0;

    while (f != NULL) {

        did_something = 1;
        if (!*first_time) fprintf(file, ",");
        else *first_time = 0;

        // Convert to REALTIVE nanoseconds since epoch for reasonable JSON numbers
        unsigned long time_ms = ((f->time.tv_sec * 1000000000L) + f->time.tv_nsec) - origin;
        fprintf(file, "{\"thread\": %d, \"time_ns\": %ld, ", f->thread_id, time_ms);

        int should_free_data = 1;
        switch(f->event_type) {
            case MALLOC:
                handle_malloc(f->data);
                break;
            case CALLOC:
                handle_calloc(f->data);
                break;
            case FREE:
                handle_free(f->data);
                should_free_data = 0; // FREE stores a pointer value, not allocated data
                break;
            case THREAD_CREATE:
                handle_pthread_create(f->data);
                break;
            case THREAD_EXIT:
                handle_pthread_exit(f->data);
                should_free_data = 0; // THREAD_EXIT stores a pointer value, not allocated data
                break;
            case EXIT:
                handle_exit(f->data);
                should_free_data = 0; // EXIT stores an integer value, not allocated data
                break;
            default:
                fprintf(stderr, "UNHANDLED EVENT SPOTTED\n");
                exit(1);
        }

        event* next = f->next;

        if (should_free_data) {
            free(f->data);
        }
        free(f);
        f = next;
    }
    if (did_something) fprintf(file, "\n");
}

static void* thread_loop(void* arg) {
    int first_time = 1;
    while (keep_looping) {
        flush_events(&first_time);
    }
    return NULL;
}


pthread_t thread;

__attribute__((constructor))
void init(void) {
    keep_looping = 1;
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    first = NULL;
    last = NULL;

    char* log_location = getenv("LD_PRELOAD_LOG");
    if (log_location == NULL || log_location[0] == '\0') log_location = "./mem-events.json";

    file = fopen(log_location, "w");
    if (file == NULL) {
        fprintf(stderr, "FAILED TO CREATE LOG FILE\n");
        exit(1);
    }
    
    fprintf(file, "[");
    pthread_create(&thread, NULL, thread_loop, NULL);
}

__attribute__((destructor))
void fini(void) {
    //Behavior here runs whenn the library unloads, after execution is over.
    pthread_mutex_lock(&lock);
    keep_looping = 0;
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);

    pthread_join(thread, NULL);

    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    fprintf(file, "]");
    fclose(file);
}
