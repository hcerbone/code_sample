Address Sanitizer keeps track of mallocs and frees by marking freed memory and
areas around `malloc`ed memory as poisoned (namely 32 bytes + padding before and
after the `malloc`ed region, where padding ensures those bytes are 32-aligned).
It stores what bytes are "poisoned" in a separate area of memory known as
"shadow memory" below main application memory; heap shadow memory lies below,
and global/stack shadow memory lies above. The correspondence is chosen so that
there is a gap between the heap's shadow memory and the global/stack's shadow
memory, and so that attempting to find "shadow memory" corresponding to shadow
memory lands in an unaddressable region. Shadow memory encodes how many bytes of
main application memory are poisoned.

Each byte of shadow memory corresponds to 8 bytes of main application memory; if
none of those bytes are poisoned, they are all addressable, and the shadow
memory byte has value 0. If they are all poisoned, the shadow memory byte has
some negative value. If only some first `k` bytes are unpoisoned, the shadow
memory byte has value `k` (we don't need to consider the last case because
`malloc` returns 8-aligned values, so we can only have a half-poisoned 8 byte
area at the tail of a `malloc`ed region). So every `malloc` call allocates up to
`sz + 96` bytes and sets shadow memory appropriately for each 8-byte block.
Every `free` poisons those `sz` bytes and quarantines memory so we can catch
double frees. In the stack, when local variables are allocated, the sanitizer
makes sure to unpoison the poisoned areas around the allocated address when we
exit the block.

When attempting to access or set memory, the sanitizer first checks what the
shadow memory says; if accessing a poisoned byte, it reports an error and

There is additionally a feature of AddressSanitizer to do run-time memory leak detection. It utilizes LeakSanitizer (packaged in AddressSanitizer) to achieve this funcitonality. The LSan does not run until a process' lifetime is over. At this time, it halts the execution process. Transient pointers are then examined. The following steps are looking at the root steps of live memory. This involves global variables, stacks of running threads, general-purpose registers of running threads, ELF thread-local storage and POSIX thread-specific data. These areas are searched for annything resembling a pointer.  This is done iteratively to then search any "pointed-to" blocks to ensure that all blocks are scanned. Through this process, LSan can distinguish from independent leaked blocks of memory or those that are inherited/linked. LSan can be configured to run with other sanitizers (the obvious being AddressSanitizer) as well as ignoring leaks of specific kinds. 

Sources:
https://github.com/google/sanitizers/wiki/AddressSanitizer
https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizerDesignDocument
