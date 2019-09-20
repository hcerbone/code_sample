#define M61_DISABLE 1
#include "m61.hh"
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// statistic global variables
unsigned long malloc_counter = 0;
unsigned long free_counter = 0;
unsigned long active_counter = 0;
unsigned long fail_counter = 0;
size_t active_size_acc = 0;
size_t fail_size_acc = 0;
size_t total_size_acc = 0;
uintptr_t heap_min_track = ULONG_MAX;
uintptr_t heap_max_track = 0;

// magic numbers
unsigned long magic_header = 12345678;
unsigned long magic_free = 121234345656;
char magic_footer[] = "checkout";

// meta_data that will be appended as a header to the value
struct meta_data {
  size_t alloc_size;
  unsigned long status_tag;
  const char *alloc_file;
  long alloc_line;
  meta_data *prev_ptr;
  meta_data *next_ptr;
};
meta_data first_node = {0, 0, nullptr, 0, nullptr, nullptr};
meta_data *inter_ptr = &first_node;
// size constants
size_t meta_data_sz = sizeof(meta_data) + sizeof(meta_data) % 16;
size_t magic_footer_sz = sizeof(magic_footer);

/// m61_malloc(sz, file, line)
///    Return a pointer to `sz` bytes of newly-allocated dynamic memory.
///    The memory is not initialized. If `sz == 0`, then m61_malloc must
///    return a unique, newly-allocated pointer value. The allocation
///    request was at location `file`:`line`.

void *m61_malloc(size_t sz, const char *file, long line) {
  (void)file, (void)line; // avoid uninitialized variable warnings
  if (sz < UINT_MAX) {
    if (inter_ptr->next_ptr == nullptr) {

      inter_ptr->prev_ptr = inter_ptr;
      inter_ptr->next_ptr = inter_ptr;
    }
    // create meta_data header
    size_t total_size = meta_data_sz + sz + magic_footer_sz;
    void *ptr = base_malloc(total_size);
    uintptr_t ptr_addr = (uintptr_t)ptr;

    meta_data *header_ptr = (meta_data *)(ptr);
    void *payload_ptr = (void *)(ptr_addr + meta_data_sz);
    void *footer_ptr = (void *)(ptr_addr + meta_data_sz + sz);

    memcpy(footer_ptr, &magic_footer, magic_footer_sz);

    header_ptr->alloc_size = sz;
    header_ptr->status_tag = magic_header;
    header_ptr->alloc_line = line;
    header_ptr->alloc_file = file;

    // linked list updates
    header_ptr->prev_ptr = inter_ptr->prev_ptr;
    header_ptr->next_ptr = inter_ptr;
    inter_ptr->prev_ptr->next_ptr = header_ptr;
    inter_ptr->prev_ptr = header_ptr;

    // update statistics
    total_size_acc += sz;
    active_size_acc += sz;
    ++malloc_counter;
    uintptr_t addr = (uintptr_t)(payload_ptr);
    if ((addr + sz) > heap_max_track) {
      heap_max_track = addr + sz;
    }
    if (addr < heap_min_track) {
      heap_min_track = addr;
    }
    return payload_ptr;
  } else {
    ++fail_counter;
    fail_size_acc += sz;
    return nullptr;
  }
}

/// m61_free(ptr, file, line)
///    Free the memory space pointed to by `ptr`, which must have been
///    returned by a previous call to m61_malloc. If `ptr == NULL`,
///    does nothing. The free was called at location `file`:`line`.

void m61_free(void *ptr, const char *file, long line) {
  (void)file, (void)line; // avoid uninitialized variable warnings
  uintptr_t ptr_addr = (uintptr_t)ptr;
  if (ptr) {
    if (!(heap_min_track <= ptr_addr && ptr_addr <= heap_max_track)) {
      fprintf(stderr,
              "MEMORY BUG: %s:%lu: invalid free of pointer %p, not in heap\n",
              file, line, ptr);
      abort();
    } else {
      meta_data *header = (meta_data *)(ptr_addr - meta_data_sz);

      if (ptr_addr % 16 != 0 || (header->status_tag != magic_header &&
                                 header->status_tag != magic_free)) {
        fprintf(stderr,
                "MEMORY BUG: %s:%lu: invalid free of pointer %p, not allocated",
                file, line, ptr);
        meta_data *curr_ptr = inter_ptr->prev_ptr;
        while (curr_ptr != nullptr && curr_ptr != inter_ptr) {
          uintptr_t curr_addr = (uintptr_t)curr_node + meta_data_sz;
          if (ptr_addr >= curr_addr &&
              ptr_addr < curr_addr + curr_ptr->alloc_size)
            curr_ptr = curr_ptr->prev_ptr;
        }
        abort();
      } else {
        void *footer_ptr = (void *)(ptr_addr + header->alloc_size);
        if (memcmp(footer_ptr, &magic_footer, magic_footer_sz) != 0) {
          fprintf(stderr,
                  "MEMORY BUG:  %s:%lu: detected wild write during free of "
                  "pointer %p\n",
                  file, line, ptr);
          abort();
        } else if (header->status_tag != magic_free) {
          header->status_tag = magic_free;
          ++free_counter;
          active_size_acc -= header->alloc_size;
          header->prev_ptr->next_ptr = header->next_ptr;
          header->next_ptr->prev_ptr = header->prev_ptr;
          base_free((void *)header);
        } else {
          fprintf(stderr,
                  "MEMORY BUG: %s:%lu: invalid free of pointer %p, double free",
                  file, line, ptr);
          abort();
        }
      }
    }
  }
}

/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. If `sz == 0`,
///    then must return a unique, newly-allocated pointer value. Returned
///    memory should be initialized to zero. The allocation request was at
///    location `file`:`line`.

void *m61_calloc(size_t nmemb, size_t sz, const char *file, long line) {
  if (nmemb < UINT_MAX) {
    void *ptr = m61_malloc(nmemb * sz, file, line);
    if (ptr) {
      memset(ptr, 0, nmemb * sz);
    }
    return ptr;
  } else {
    ++fail_counter;
    return nullptr;
  }
}

/// m61_get_statistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_get_statistics(m61_statistics *stats) {
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
  meta_data *curr_pointer = inter_ptr->prev_ptr;
  while (curr_pointer != nullptr && curr_pointer != inter_ptr) {
    fprintf(stdout, "LEAK CHECK: %s:%lu: allocated object %p with size %lu\n",
            curr_pointer->alloc_file, curr_pointer->alloc_line,
            (char *)curr_pointer + meta_data_sz, curr_pointer->alloc_size);
    curr_pointer = curr_pointer->prev_ptr;
  }
}

/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
  // Your heavy-hitters code here
}
