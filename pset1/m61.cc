#define M61_DISABLE 1
#include "m61.hh"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <climits>
#include <cinttypes>
#include <cassert>

int malloc_counter = 0;
int free_counter = 0;
int active_counter = 0;
int fail_counter = 0;
size_t active_size_acc = 0;
size_t fail_size_acc = 0;
size_t total_size_acc = 0;
uintptr_t heap_min_track = ULONG_MAX;
uintptr_t heap_max_track = 0;

struct meta_data{
    size_t alloc_size; //8 bytes
    bool allocated = 0; // 1 bit
    void* payload; //8 bytes
}; //24 bytes

/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void* m61_malloc(size_t sz, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    if(sz < UINT_MAX) {
        ++malloc_counter;
        meta_data* info_ptr = (meta_data*)(base_malloc(24 + sz));
        info_ptr->payload = (void*)(info_ptr + 24);
        info_ptr->alloc_size = sz;
        info_ptr->allocated = 1;
        total_size_acc += sz;
        active_size_acc += sz;
        uintptr_t addr = (uintptr_t)(info_ptr->payload);
        if((addr + sz) > heap_max_track){
            heap_max_track = addr + sz;
        }
        if(addr < heap_min_track){
            heap_min_track = addr;
        }
        return info_ptr->payload;
    }
    else{
        ++fail_counter;
        fail_size_acc += sz;
        return nullptr;
    }
}


/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void m61_free(void* ptr, const char* file, long line) {
    (void) file, (void) line;   // avoid uninitialized variable warnings
    if(ptr){
        meta_data* header =  (meta_data*)(ptr)  - 24;
        ++free_counter;
        active_size_acc -= header -> alloc_size;
        if(header -> allocated == 1)
        {
            header -> allocated = 0;
            base_free(header);
        }
        else
        {
        fprintf(stderr, "MEMORY BUG: %s: invalid free of pointer , not in heap\n", file);
        abort();
        }
    }
}


/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void* m61_calloc(size_t nmemb, size_t sz, const char* file, long line) {
    if(nmemb < UINT_MAX)
    {
        void* ptr = m61_malloc(nmemb * sz, file, line);
        if (ptr) {
            memset(ptr, 0, nmemb * sz);
        }
        return ptr;
    }
    else
    {
        ++fail_counter;
        return nullptr;
    }

}


/// m61_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_get_statistics(m61_statistics* stats) {
    // Stub: set all statistics to enormous numbers
    memset(stats, 255, sizeof(m61_statistics));
    stats->nactive = malloc_counter - free_counter;
    stats->active_size = active_size_acc;
    stats->ntotal = malloc_counter;
    stats->total_size = total_size_acc;
    stats->nfail = fail_counter;
    stats->fail_size = fail_size_acc;
    stats->heap_min = heap_min_track;
    stats->heap_max = heap_max_track;
}


/// m61_print_statistics()
///    Print the current memory statistics.

void m61_print_statistics() {
    m61_statistics stats;
    m61_get_statistics(&stats);

    printf("alloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("alloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}


/// m61_print_leak_report()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_print_leak_report() {
    // Your code here.
}


/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
    // Your heavy-hitters code here
}
