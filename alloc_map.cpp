#include "alloc_map.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <sys/types.h>

// Internal C++ structure
struct MemoryEventInternal {
    unsigned long timestamp_ns;
    int event_type;
    void* related_ptr;
    size_t size;
};

// Two-level map: thread_id -> (pointer -> event_history)
using PointerMap = std::unordered_map<void*, std::vector<MemoryEventInternal>>;
using ThreadMap = std::unordered_map<pid_t, PointerMap>;

struct AllocMap {
    ThreadMap data;
    std::mutex lock;
};

// Global instance
static AllocMap* g_alloc_map = nullptr;

extern "C" {

void alloc_map_init(void) {
    if (!g_alloc_map) {
        g_alloc_map = new AllocMap();
    }
}

void alloc_map_destroy(void) {
    if (g_alloc_map) {
        delete g_alloc_map;
        g_alloc_map = nullptr;
    }
}

void alloc_map_add_event(pid_t thread_id, void* ptr, int event_type,
                         struct timespec* timestamp_ns, void* related_ptr, size_t size) {
    if (!g_alloc_map || !ptr) return;
    
    std::lock_guard<std::mutex> guard(g_alloc_map->lock);
    
    MemoryEventInternal event;
    event.timestamp_ns = (timestamp_ns->tv_sec * 1000000000UL) + timestamp_ns->tv_nsec;
    event.event_type = event_type;
    event.related_ptr = related_ptr;
    event.size = size;
    
    g_alloc_map->data[thread_id][ptr].push_back(event);
}

int alloc_map_get_history(pid_t thread_id, void* ptr, MemoryEvent* events, int max_events) {
    if (!g_alloc_map || !ptr) return -1;
    
    std::lock_guard<std::mutex> guard(g_alloc_map->lock);
    
    auto thread_it = g_alloc_map->data.find(thread_id);
    if (thread_it == g_alloc_map->data.end()) {
        return -1;
    }
    
    auto ptr_it = thread_it->second.find(ptr);
    if (ptr_it == thread_it->second.end()) {
        return -1;
    }
    
    const auto& history = ptr_it->second;
    int count = static_cast<int>(history.size());
    
    if (events && max_events > 0) {
        int to_copy = (count < max_events) ? count : max_events;
        for (int i = 0; i < to_copy; i++) {
            events[i].timestamp_ns = history[i].timestamp_ns;
            events[i].event_type = history[i].event_type;
            events[i].related_ptr = history[i].related_ptr;
            events[i].size = history[i].size;
        }
    }
    
    return count;
}

void alloc_map_remove(pid_t thread_id, void* ptr) {
    if (!g_alloc_map || !ptr) return;
    
    std::lock_guard<std::mutex> guard(g_alloc_map->lock);
    
    auto thread_it = g_alloc_map->data.find(thread_id);
    if (thread_it != g_alloc_map->data.end()) {
        thread_it->second.erase(ptr);
        
        // Remove thread entry if empty
        if (thread_it->second.empty()) {
            g_alloc_map->data.erase(thread_it);
        }
    }
}

void alloc_map_clear_thread(pid_t thread_id) {
    if (!g_alloc_map) return;
    
    std::lock_guard<std::mutex> guard(g_alloc_map->lock);
    g_alloc_map->data.erase(thread_id);
}

int alloc_map_size(void) {
    if (!g_alloc_map) return 0;
    
    std::lock_guard<std::mutex> guard(g_alloc_map->lock);
    
    int total = 0;
    for (const auto& thread_pair : g_alloc_map->data) {
        total += thread_pair.second.size();
    }
    
    return total;
}

} // extern "C"
