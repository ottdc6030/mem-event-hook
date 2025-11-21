#pragma once
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void enable_new_behavior(void);

void disable_new_behavior(void);

int use_new_behavior(void);


/**
 * Checks if the reference to the "real" (original) version of a function is null, and assigning the actual address if it is null.
 * You only need to invoke this if you plan to call the "real" version of an overridden funciton OUTSIDE the replacement function.
 */
#define ASSERT_REAL(name) \
    if (!real_##name) {   \
        real_##name = dlsym(RTLD_NEXT, #name); \
        if (!real_##name) { \
            fprintf(stderr, "dlsym failed for '%s'\n", #name); \
            exit(1); \
        } \
    }                                           

/**
 * Overrides a specific NON-VOID function with new behavior.
 * If new behavior is enabled for a thread, your new user-specified behavior will be called instead of the original behavior.
 * If new behavior is disabled, the original "real" behavior will run.
 * 
 * Even when new behavior is enabled, a pointer to the original behavior will be available with the "real_" prefix.
 * For example, overriding malloc() allows the new behavior to call the original via real_malloc()
 * If for any reason you need to do recursion on new behavior (unlikely), the pointer to new behavior has the "new_" prefix (ex. new_malloc())
 * 
 * During execution of your override function, new behavior is temporarily disabled. 
 * This is to prevent any behavior within the overrides from contaminating the data we're trying to gather on the target application.
 */
#define OVERRIDE(ret, name, args, call_args)                 \
    typedef ret (*name##_t) args;                                   \
    static name##_t real_##name = NULL;                             \
                                                                    \
    ret new_##name args;                                            \
                                                                    \
    ret name args {                                                 \
        ASSERT_REAL(name)                                           \
        if (use_new_behavior()) {                                   \
            disable_new_behavior();                                 \
            ret send = new_##name call_args;                        \
            enable_new_behavior();                                  \
            return send;                                            \
        }                                                           \
        else {                                                      \
            return real_##name call_args;                           \
        }                                                           \
    }                                                               \
    ret new_##name args


/**
 * Overrides a specific VOID function with new behavior.
 * If new behavior is enabled for a thread, your new user-specified behavior will be called instead of the original behavior.
 * If new behavior is disabled, the original "real" behavior will run.
 * 
 * Even when new behavior is enabled, a pointer to the original behavior will be available with the "real_" prefix.
 * For example, overriding malloc() allows the new behavior to call the original via real_malloc()
 * If for any reason you need to do recursion (unlikely), the pointer to your own new behavior has the "new_" prefix (ex. new_malloc)
 * 
 * During execution of your override function, new behavior is temporarily disabled. 
 * This is to prevent any other overriden functions from contaminating the data we're trying to gather on the target application.
 */
#define V_OVERRIDE(name, args, call_args)                           \
    typedef void (*name##_t) args;                                  \
    static name##_t real_##name = NULL;                             \
                                                                    \
    void new_##name args;                                           \
                                                                    \
    void name args {                                                \
        ASSERT_REAL(name)                                           \
        if (use_new_behavior()) {                                   \
            disable_new_behavior();                                 \
            new_##name call_args;                                   \
            enable_new_behavior();                                  \
        }                                                           \
        else {                                                      \
            real_##name call_args;                                  \
        }                                                           \
    }                                                               \
    void new_##name args

/** 
 * A variation of V_OVERRIDE
 * This is specifically meant to solve compilation issues when overriding functions that never return.
*/
#define V_OVERRIDE_NORETURN(name, args, call_args) \
    typedef void (*name##_t) args;                                  \
    static name##_t real_##name = NULL;                             \
                                                                    \
    __attribute__((noreturn)) void new_##name args;                 \
                                                                    \
    __attribute__((noreturn)) void name args {                      \
        ASSERT_REAL(name)                                           \
        disable_new_behavior();                                     \
        new_##name call_args;                                       \
    }                                                               \
    __attribute__((noreturn)) void new_##name args

/**
 * Overrides a specific NON-VOID function with new behavior.
 * Unlike OVERRIDE, the new behavior always runs regardless of if new behavior is supposed to be enabled for that thread.
 * Within execution, the "new behavior" flag is still disabled, but will return to its orignal state (on or off) after execution.
 * But like OVERRIDE, the original behavior is still accessible with the "real_" prefix, and the new function is accessible with the "new_" prefix for recursion.
 */
#define OVERRIDE_ALWAYS(ret, name, args, call_args) \
    typedef ret (*name##_t) args;                                   \
    static name##_t real_##name = NULL;                             \
                                                                    \
    ret new_##name args;                                            \
                                                                    \
    ret name args {                                                 \
        ASSERT_REAL(name)                                           \
        int flag = use_new_behavior();                              \
        disable_new_behavior();                                     \
        ret send = new_##name call_args;                            \
        if (flag) enable_new_behavior();                            \
        return send;                                                \
    }                                                               \
    ret new_##name args

/**
 * Overrides a specific VOID function with new behavior.
 * Unlike OVERRIDE, the new behavior always runs regardless of if new behavior is supposed to be enabled for that thread.
 * Within execution, the "new behavior" flag is still disabled, but will return to its orignal state (on or off) after execution.
 * But like OVERRIDE, the original behavior is still accessible with the "real_" prefix, and the new function is accessible with the "new_" prefix for recursion.
 */
#define V_OVERRIDE_ALWAYS(name, args, call_args) \
    typedef void (*name##_t) args;                                  \
    static name##_t real_##name = NULL;                             \
                                                                    \
    void new_##name args;                                           \
                                                                    \
    void name args {                                                \
        ASSERT_REAL(name)                                           \
        int flag = use_new_behavior();                              \
        disable_new_behavior();                                     \
        new_##name call_args;                                       \
        if (flag) enable_new_behavior();                            \
    }                                                               \
    void new_##name args

/**
 * Forks are a special case. Children are expected to execute a separate program, meaning the library setup will start all over again.
 * So no new behavior should not be recorded in a child when all it is doing is getting ready to execute something else.
 */
#define ON_FORK \
    static int (*real_fork)(void); \
    int new_fork(void);             \
                                    \
    int fork(void) {                \
        ASSERT_REAL(fork)           \
        disable_new_behavior();     \
        int ret = new_fork();       \
        if (ret > 0) enable_new_behavior(); \
        return ret;                 \
    }                               \
                                    \
    int new_fork(void)


/**
 * Overrides the int main() function. You have access to its parameters (int argc, char** argv, char** envp)
 * Pointer to orignal function is real_main
 */
#define OVERRIDE_MAIN \
    typedef int (*main_fn_t)(int, char**, char**); \
    int (*real___libc_start_main)(main_fn_t, int, char**, void(*)(), void(*)(), void(*)(), void*) = NULL; \
    int new_main(int argc, char** argv, char** envp); \
                                                          \
    main_fn_t real_main = NULL;                           \
                                                          \
    int __libc_start_main(                                \
    main_fn_t main,                                       \
    int argc,                                             \
    char** argv,                                          \
    void (*init)(void),                                   \
    void (*fini)(void),                                   \
    void (*rtld_fini)(void),                              \
    void* stack_end)                                      \
{                                                         \
    real_main = main;                                     \
    ASSERT_REAL(__libc_start_main)                          \
    return real___libc_start_main(new_main, argc, argv, init, fini, rtld_fini, stack_end); \
} \
    int new_main(int argc, char** argv, char** envp)


