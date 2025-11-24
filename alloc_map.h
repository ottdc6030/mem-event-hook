#pragma once
#include "event_queue.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for C code
typedef struct AllocMap AllocMap;

// Event entry in the history
typedef struct {
    long long timestamp_ns;
    int event_type;  // From OVERRIDE_ID enum
    void* related_ptr;  // For realloc/memcpy operations
    size_t size;
} MemoryEvent;

// Initialize the global allocation map
void alloc_map_init(void);

// Clean up the global allocation map
void alloc_map_destroy(void);

// Add an allocation event for a specific thread and pointer
void alloc_map_add_event(pid_t thread_id, void* ptr, int event_type, 
                         struct timespec* timestamp_ns, void* related_ptr, size_t size);

// Get the history for a specific thread and pointer
// Returns the number of events, or -1 if not found
// If events is NULL, just returns the count
int alloc_map_get_history(pid_t thread_id, void* ptr, MemoryEvent* events, int max_events);

// Remove an entry (e.g., after free)
void alloc_map_remove(pid_t thread_id, void* ptr);

// Clear all entries for a specific thread (e.g., thread exit)
void alloc_map_clear_thread(pid_t thread_id);

// Get total number of tracked allocations across all threads
int alloc_map_size(void);

#ifdef __cplusplus
}
#endif
