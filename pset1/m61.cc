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

struct loc {
  const char *file;
  long line;
};
unsigned long long byte_counters[6];
unsigned long long alloc_freqs[6];
loc count_locs[6];
loc freq_locs[6];

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

    bool fnd_cnt = false;
    bool fnd_freq = false;
    int zero_cnt = -1;
    int zero_frq = -1;
    unsigned long long min_size = sz;
    int min_counter = -1;
    for (int i = 0; i < 6; ++i) {
      if (byte_counters[i] == 0) {
        zero_cnt = i;
      } else {
        if (count_locs[i].file == file && count_locs[i].line == line) {
          byte_counters[i] += sz;
          fnd_cnt = true;
          break;
        }
        if (min_size > byte_counters[i]) {
          min_size = byte_counters[i];
          min_counter = i;
        }
      }
    }
    for (int i = 0; i < 6; ++i) {
      if (alloc_freqs[i] == 0) {
        zero_frq = i;
      } else {
        if (freq_locs[i].file == file && freq_locs[i].line == line) {
          alloc_freqs[i] += 1;
          fnd_freq = true;
          break;
        }
      }
    }
    if (!fnd_cnt) {
      if (zero_cnt != -1) {
        count_locs[zero_cnt] = {file, line};
        byte_counters[zero_cnt] = sz;
      } else {
        for (int i = 0; i < 6; ++i) {
          byte_counters[i] -= min_size;
          if (min_counter == i) {
            byte_counters[i] = sz - min_size;
            count_locs[i] = {file, line};
          }
        }
      }
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
      meta_data *header_ptr = (meta_data *)(ptr_addr - meta_data_sz);

      if (ptr_addr % 16 != 0 || (header_ptr->status_tag != magic_header &&
                                 header_ptr->status_tag != magic_free)) {
        fprintf(
            stderr,
            "MEMORY BUG: %s:%lu: invalid free of pointer %p, not allocated\n",
            file, line, ptr);
        meta_data *curr_ptr = inter_ptr->prev_ptr;
        while (curr_ptr != nullptr && curr_ptr != inter_ptr) {
          uintptr_t curr_addr = (uintptr_t)curr_ptr + meta_data_sz;
          if (ptr_addr >= curr_addr &&
              ptr_addr < curr_addr + curr_ptr->alloc_size)
            fprintf(stderr,
                    "  %s:%li: %p is %lu bytes inside a %lu byte region "
                    "allocated here",
                    curr_ptr->alloc_file, curr_ptr->alloc_line, ptr,
                    ptr_addr - curr_addr, curr_ptr->alloc_size);
          curr_ptr = curr_ptr->prev_ptr;
        }
        abort();
      } else {
        if (header_ptr->status_tag == magic_free) {
          fprintf(stderr,
                  "MEMORY BUG: %s:%lu: invalid free of pointer %p, double free",
                  file, line, ptr);
          abort();
        }
      }
      if (header_ptr->prev_ptr->next_ptr != header_ptr ||
          header_ptr->next_ptr->prev_ptr != header_ptr) {
        fprintf(stderr,
                "MEMORY BUG: %s: %lu: invalid free of pointer %p, not "
                "allocated\n",
                file, line, ptr);
        abort();
      }
      void *footer_ptr = (void *)(ptr_addr + header_ptr->alloc_size);
      if (memcmp(footer_ptr, &magic_footer, magic_footer_sz) != 0) {
        fprintf(stderr,
                "MEMORY BUG:  %s:%lu: detected wild write during free of "
                "pointer %p\n",
                file, line, ptr);
        abort();
      } else if (header_ptr->status_tag != magic_free) {
        header_ptr->status_tag = magic_free;
        ++free_counter;
        active_size_acc -= header_ptr->alloc_size;
        header_ptr->prev_ptr->next_ptr = header_ptr->next_ptr;
        header_ptr->next_ptr->prev_ptr = header_ptr->prev_ptr;
        base_free((void *)header_ptr);
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
  meta_data *curr_ptr = inter_ptr->prev_ptr;
  while (curr_ptr != nullptr && curr_ptr != inter_ptr) {
    fprintf(stdout, "LEAK CHECK: %s:%lu: allocated object %p with size %lu\n",
            curr_ptr->alloc_file, curr_ptr->alloc_line,
            (char *)curr_ptr + meta_data_sz, curr_ptr->alloc_size);
    curr_ptr = curr_ptr->prev_ptr;
  }
}

/// m61_print_heavy_hitter_report()
///    Print a report of heavily-used allocation locations.

void m61_print_heavy_hitter_report() {
  // Your heavy-hitters code here
  for (int i = 6; i > 0; --i) {
    int cnt_curr_max_idx = 0;
    for (int j = 0; j < i; ++j) {
      if (byte_counters[cnt_curr_max_idx] < byte_counters[j]) {
        cnt_curr_max_idx = j;
      }
    }
    if (count_locs[cnt_curr_max_idx].file != nullptr) {
      double percent =
          (double)byte_counters[cnt_curr_max_idx] / total_size_acc * 100;
      fprintf(stdout,
              "HEAVY HITTER: %s:%lu: %llu bytes (approx %.2f%%) of %lu bytes\n",
              count_locs[cnt_curr_max_idx].file,
              count_locs[cnt_curr_max_idx].line,
              byte_counters[cnt_curr_max_idx], percent, total_size_acc);
    }
    byte_counters[cnt_curr_max_idx] = byte_counters[i - 1];
    count_locs[cnt_curr_max_idx] = count_locs[i - 1];
  }
  for (int i = 6; i > 0; --i) {
    int freq_curr_max_idx = 0;
    for (int j = 0; j < i; ++j) {
      if (alloc_freqs[freq_curr_max_idx] < alloc_freqs[j]) {
        freq_curr_max_idx = j;
      }
    }
    if (freq_locs[freq_curr_max_idx].file != nullptr) {
      double percent =
          (double)alloc_freqs[freq_curr_max_idx] / malloc_counter * 100;
      fprintf(stdout,
              "HEAVY HITTER: %s:%li: %llu allocations (approx %.2f%%) of %lu "
              "allocations\n",
              freq_locs[freq_curr_max_idx].file,
              freq_locs[freq_curr_max_idx].line, alloc_freqs[freq_curr_max_idx],
              percent, malloc_counter);
    }
    alloc_freqs[freq_curr_max_idx] = alloc_freqs[i - 1];
    freq_locs[freq_curr_max_idx] = freq_locs[i - 1];
  }
}
