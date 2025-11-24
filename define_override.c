#define _GNU_SOURCE
#include <unistd.h>
#include "define_override.h"
#include "alloc_map.h"
#include "event_queue.h"
#include <pthread.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/sched.h>
#include <sys/syscall.h>
#include <sched.h>
#include <time.h>


static __thread int new_behavior = 0;
static __thread struct timespec time_buffer;

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
    push_event(MALLOC, data, &time_buffer);
    alloc_map_add_event(gettid(), send, MALLOC, &time_buffer, NULL, size);
    return send;
}

OVERRIDE(void*, calloc, (size_t mem_count, size_t mem_size), (mem_count, mem_size)) {
    void* send = real_calloc(mem_count, mem_size);

    void** data = malloc(3*sizeof(void*));
    data[0] = (void*)mem_count;
    data[1] = (void*)mem_size;
    data[2] = send;
    push_event(CALLOC, data, &time_buffer);
    alloc_map_add_event(gettid(), send, CALLOC, &time_buffer, NULL, mem_count*mem_size);
    return send;
}

OVERRIDE(void*, realloc, (void* ptr, size_t size), (ptr, size)) {
    void* send = real_realloc(ptr, size);

    void** data = malloc(3*sizeof(void*));
    data[0] = ptr;
    data[1] = (void*)size;
    data[2] = send;
    push_event(REALLOC, data, &time_buffer);
    alloc_map_add_event(gettid(), send, REALLOC, &time_buffer, ptr, size);
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

    push_event(MMAP, data, &time_buffer);
    alloc_map_add_event(gettid(), send, MMAP, &time_buffer, addr, len);
    return send;
}

OVERRIDE(int, munmap, (void* addr, size_t size), (addr, size)) {
    int send = real_munmap(addr, size);

    void** data = malloc(3*sizeof(void));
    data[0] = addr;
    data[1] = (void*) size;
    data[2] = (void*)((long)send);

    push_event(MUNMAP, data, &time_buffer);
    alloc_map_add_event(gettid(), addr, MUNMAP, &time_buffer, NULL, send);
    return send;
}


V_OVERRIDE(free, (void* arg), (arg)) {
    push_event(FREE, arg, &time_buffer);
    real_free(arg);
}


OVERRIDE(void*, memcpy, (void* dest, const void* src, size_t n), (dest, src, n)) {
    void** data = malloc(3*sizeof(void*));
    data[0] = dest;
    data[1] = (void*)src;
    data[2] = (void*)n;

    push_event(MEMCPY, data, &time_buffer);

    return real_memcpy(dest, src, n);
}



OVERRIDE(char*, strncpy, (char* dest, const char* src, size_t n), (dest, src, n)) {
    void** data = malloc(3*sizeof(void*));
    data[0] = dest;
    data[1] = (void*)src;
    data[2] = (void*)n;

    push_event(STRNCPY, data, &time_buffer);

    return real_strncpy(dest, src, n);
}



/**
 * When a new thread spawns using pthread_create(), this wrapper function is passed in instead
 * It still calls the original function, but allows for some monitoring before and after exectution.
 */
static void* thread_wrapper(void* thread_pack) {
    void* stackBase = __builtin_frame_address(0);

    void** pack = thread_pack;

    void* (*func)(void*) = pack[0];
    void* arg = pack[1];

    pack[3] = stackBase;

    push_event(THREAD_CREATE, pack, &time_buffer);
    enable_new_behavior();

    void* send = func(arg);
    disable_new_behavior();
    push_event(THREAD_EXIT, send, &time_buffer);
    return send;
}

OVERRIDE(int, pthread_create, 
    (pthread_t *__restrict__ __newthread, 
    const pthread_attr_t *__restrict__ __attr, 
    void *(*__start_routine)(void *), 
    void *__restrict__ __arg),
    (__newthread, __attr, __start_routine, __arg)) {

    void** pack = malloc(4*sizeof(void*));
    pack[0] = __start_routine;
    pack[1] = __arg;
    pack[2] = (void*)((unsigned long)gettid());
    //Slot 3 will be filled with the stack base pointer once the thread starts.

    return real_pthread_create(__newthread, __attr, thread_wrapper, pack);
}

V_OVERRIDE_NORETURN(pthread_exit, (void* retval), (retval)) {
    //Called if the thread decides to terminate early
    //printf("EXITED WITH: %p\n", retval);

    push_event(THREAD_EXIT, retval, &time_buffer);
    alloc_map_clear_thread(gettid());
    end_loop();
    real_pthread_exit(retval);
    __builtin_unreachable();
}

V_OVERRIDE_NORETURN(exit, (int status), (status)) {
    unsigned long stat = status;
    push_event(EXIT, (void*) stat, &time_buffer);
    real_exit(status);
    __builtin_unreachable();
}

ON_FORK {
    int pid = real_fork();
    if (pid > 0) {
        long child = pid;
        push_event(FORK, (void*)child, &time_buffer);
    }
    return pid;
}

ON_VFORK {
    end_loop();

    int pid = real_vfork();
    if (pid > 0) {
        long child = pid;
        child |= (1L << 32);
        push_event(FORK, (void*) child, &time_buffer);
        restart_loop();
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
    push_event(THREAD_CREATE, dummy_arg, &time_buffer);

    enable_new_behavior();

    int ret = real_main(argc, argv, envp);

    disable_new_behavior();

    unsigned long val = ret;
    push_event(THREAD_EXIT, (void*)val, &time_buffer);
    return ret;
}


typedef long (*syscall_fn)(long number, ...);
typedef long (*syscall_listener)(long number, va_list ap);

static syscall_fn real_syscall = NULL;
static syscall_listener syscall_monitors[470] = {0L};

long syscall(long number, ...) {
    if (!real_syscall) real_syscall = dlsym(RTLD_NEXT, "syscall");

    va_list ap;
    va_start(ap, number);

    long ret;

    if (use_new_behavior()) {
        disable_new_behavior();
        syscall_listener listener = syscall_monitors[number];
        if (listener) {
            ret = listener(number, ap);
        }
        else {
            ret = real_syscall(number, ap);
        }
        enable_new_behavior();
    }
    else {
        ret = real_syscall(number, ap);
    }

    va_end(ap);
    return ret;
}

//Clone3
ON_SYSCALL(435) {
    end_loop();
    va_list copy;
    va_copy(copy, ap);

    struct clone_args* cl_args = va_arg(copy, struct clone_args*);
    size_t size = va_arg(copy, size_t);

    long ret = real_syscall(435, ap);

    if (ret > 0) {
        unsigned long* send = malloc(sizeof(struct clone_args) + sizeof(size_t) + sizeof(long));
        send[0] = cl_args->flags;
        send[1] = cl_args->pidfd;
        send[2] = cl_args->child_tid;
        send[3] = cl_args->parent_tid;
        send[4] = cl_args->exit_signal;
        send[5] = cl_args->stack;
        send[6] = cl_args->stack_size;
        send[7] = cl_args->tls;
        send[8] = cl_args->set_tid;
        send[9] = cl_args->set_tid_size;
        send[10] = cl_args->cgroup;
        send[11] = size;
        send[12] = ret;
        push_event(CLONE3, send, &time_buffer);
        restart_loop();
    }

    va_end(copy);
    return ret;
}