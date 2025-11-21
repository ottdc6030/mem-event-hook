#define _GNU_SOURCE
#include <unistd.h>
#include "define_override.h"
#include "event_queue.h"
#include <pthread.h>
#include <sys/mman.h>
#include <string.h>


static __thread int new_behavior = 0;

void enable_new_behavior(void) {
    new_behavior = 1;
}

void disable_new_behavior(void) {
    new_behavior = 0;
}

int use_new_behavior(void) {
    return new_behavior;
}


OVERRIDE(void*, malloc, (size_t size), (size)) {
    //printf("MALLOC %ld\n", size);
    void* send = real_malloc(size);
    
    void** data = real_malloc(2*sizeof(void*));
    data[0] = (void*)size;
    data[1] = (void*)send;
    push_event(MALLOC, data);

    return send;
}

OVERRIDE(void*, calloc, (size_t mem_count, size_t mem_size), (mem_count, mem_size)) {
    void* send = real_calloc(mem_count, mem_size);

    void** data = malloc(3*sizeof(void*));
    data[0] = (void*)mem_count;
    data[1] = (void*)mem_size;
    data[2] = send;
    push_event(CALLOC, data);

    return send;
}

OVERRIDE(void*, realloc, (void* ptr, size_t size), (ptr, size)) {
    void* send = real_realloc(ptr, size);

    void** data = malloc(3*sizeof(void*));
    data[0] = ptr;
    data[1] = (void*)size;
    data[2] = send;
    push_event(REALLOC, data);

    return send;
}

OVERRIDE(void*, mmap, (void *addr, size_t len, int prot, int flags, int fd, off_t offset), (addr, len, prot, flags, fd, offset)) {
    void* send = real_mmap(addr, len, prot, flags, fd, offset);

    mmap_data* data = malloc(sizeof(mmap_data));
    data->addr = addr;
    data->len = len;
    data->prot = prot;
    data->flags = flags;
    data->fd = fd;
    data->offset = offset;
    data->retVal = send;

    push_event(MMAP, data);

    return send;
}

OVERRIDE(int, munmap, (void* addr, size_t size), (addr, size)) {
    int send = real_munmap(addr, size);

    void** data = malloc(3*sizeof(void));
    data[0] = addr;
    data[1] = (void*) size;
    data[2] = (void*)((long)send);

    push_event(MUNMAP, data);

    return send;
}

V_OVERRIDE(free, (void* arg), (arg)) {
    push_event(FREE, arg);
    real_free(arg);
}


OVERRIDE(void*, memcpy, (void* dest, const void* src, size_t n), (dest, src, n)) {
    void** data = malloc(3*sizeof(void*));
    data[0] = dest;
    data[1] = (void*)src;
    data[2] = (void*)n;

    push_event(MEMCPY, data);

    return real_memcpy(dest, src, n);
}



OVERRIDE(char*, strncpy, (char* dest, const char* src, size_t n), (dest, src, n)) {
    void** data = malloc(3*sizeof(void*));
    data[0] = dest;
    data[1] = (void*)src;
    data[2] = (void*)n;

    push_event(STRNCPY, data);

    return real_strncpy(dest, src, n);
}



/**
 * When a new thread spawns using pthread_create(), this wrapper function is passed in instead
 * It still calls the original function, but allows for some monitoring before and after exectution.
 */
static void* thread_wrapper(void* thread_pack) {
    void* stackBase = __builtin_frame_address(0);

    void* (*func)(void*) = ((void**)thread_pack)[0];
    void* arg = ((void**)thread_pack)[1];

    ((void**)thread_pack)[3] = stackBase;

    push_event(THREAD_CREATE, thread_pack);
    enable_new_behavior();

    void* send = func(arg);
    disable_new_behavior();
    push_event(THREAD_EXIT, send);
    return send;
}

OVERRIDE(int, pthread_create, 
    (pthread_t *__restrict__ __newthread, 
    const pthread_attr_t *__restrict__ __attr, 
    void *(*__start_routine)(void *), 
    void *__restrict__ __arg),
    (__newthread, __attr, __start_routine, __arg)) {

    ASSERT_REAL(malloc);
    void** pack = real_malloc(4*sizeof(void*));
    pack[0] = __start_routine;
    pack[1] = __arg;
    pack[2] = (void*)((unsigned long)gettid());
    //Slot 3 will be filled with the stack base pointer once the thread starts.

    return real_pthread_create(__newthread, __attr, thread_wrapper, pack);
}

V_OVERRIDE_NORETURN(pthread_exit, (void* retval), (retval)) {
    //Called if the thread decides to terminate early
    printf("EXITED WITH: %p\n", retval);

    push_event(THREAD_EXIT, retval);
    real_pthread_exit(retval);
    __builtin_unreachable();
}

V_OVERRIDE_NORETURN(exit, (int status), (status)) {
    unsigned long stat = status;
    push_event(THREAD_EXIT, (void*) stat);
    real_exit(status);
    __builtin_unreachable();
}

ON_FORK {
    int pid = real_fork();
    if (pid > 0) {
        long child = pid;
        push_event(FORK, (void*)child);
    }
    return pid;
}

OVERRIDE_MAIN {
    printf("ARGS:\n");
    for (int i = 0; i < argc; i++) {
        printf("%d: %s\n", i, argv[i]);
    }

    printf("ENVS:\n");
    for (int i = 0; envp[i] != NULL; i++) {
        printf("%d: %s\n", i, envp[i]);
    }

    void** dummy_arg = malloc(4*sizeof(void*));
    dummy_arg[0] = real_main;
    dummy_arg[1] = argv;
    dummy_arg[2] = 0;
    dummy_arg[3] = __builtin_frame_address(0);
    push_event(THREAD_CREATE, dummy_arg);

    enable_new_behavior();

    int ret = real_main(argc, argv, envp);

    disable_new_behavior();

    unsigned long val = ret;
    push_event(THREAD_EXIT, (void*)val);
    return ret;
}


