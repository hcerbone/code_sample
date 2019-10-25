#include "kernel.hh"
#include "k-apic.hh"
#include "k-vmiter.hh"
#include <atomic>

// kernel.cc
//
//    This is the kernel.


// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

#define PROC_SIZE 0x40000       // initial state only

proc ptable[NPROC];             // array of process descriptors
                                // Note that `ptable[0]` is never used.
proc* current;                  // pointer to currently executing proc

#define HZ 100                  // timer interrupt frequency (interrupts/sec)
static std::atomic<unsigned long> ticks; // # timer interrupts so far


// Memory state
//    Information about physical page with address `pa` is stored in
//    `pages[pa / PAGESIZE]`. In the handout code, each `pages` entry
//    holds an `refcount` member, which is 0 for free pages.
//    You can change this as you see fit.

pageinfo pages[NPAGES];


[[noreturn]] void schedule();
[[noreturn]] void run(proc* p);
void exception(regstate* regs);
uintptr_t syscall(regstate* regs);
void memshow();


// kernel(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

static void process_setup(pid_t pid, const char* program_name);

void kernel(const char* command) {
    // initialize hardware
    init_hardware();
    log_printf("Starting WeensyOS\n");

    ticks = 1;
    init_timer(HZ);

    // clear screen
    console_clear();

    // (re-)initialize kernel page table
    for (vmiter it(kernel_pagetable);
        it.va() < MEMSIZE_PHYSICAL;
        it += PAGESIZE) {
       if (it.va() >= PROC_START_ADDR
           || it.va() == CONSOLE_ADDR) {
           it.map(it.va(), PTE_P | PTE_W | PTE_U);
       } else if (it.va() != 0) {
           it.map(it.va(), PTE_P | PTE_W);
       } else {
           it.map(it.va(), 0);
       }
    }

    // set up process descriptors
    for (pid_t i = 0; i < NPROC; i++) {
        ptable[i].pid = i;
        ptable[i].state = P_FREE;
    }
    if (command && program_loader(command).present()) {
        process_setup(1, command);
    } else {
        process_setup(1, "allocator");
        process_setup(2, "allocator2");
        process_setup(3, "allocator3");
        process_setup(4, "allocator4");
    }

    // Switch to the first process using run()
    run(&ptable[1]);
}


// kalloc(sz)
//    Kernel memory allocator. Allocates `sz` contiguous bytes and
//    returns a pointer to the allocated memory, or `nullptr` on failure.
//
//    The returned memory is initialized to 0xCC, which corresponds to
//    the x86 instruction `int3` (this may help you debug). You'll
//    probably want to reset it to something more useful.
//
//    On WeensyOS, `kalloc` is a page-based allocator: if `sz > PAGESIZE`
//    the allocation fails; if `sz < PAGESIZE` it allocates a whole page
//    anyway.
//
//    The handout code returns the next allocatable free page it can find.
//    It never reuses pages or supports freeing memory (you'll change that).


void* kalloc(size_t sz) {
    if (sz > PAGESIZE) {
        return nullptr;
    }

    uintptr_t next_alloc_pa = 0;

    while (next_alloc_pa < MEMSIZE_PHYSICAL) {
        uintptr_t pa = next_alloc_pa;
        next_alloc_pa += PAGESIZE;

        if (allocatable_physical_address(pa)
            && !pages[pa / PAGESIZE].used()) {
            pages[pa / PAGESIZE].refcount = 1;
            memset((void*) pa, 0xCC, PAGESIZE);
            return (void*) pa;
        }
    }
    return nullptr;
}


// kfree(kptr)
//    Free `kptr`, which must have been previously returned by `kalloc`.
//    If `kptr == nullptr` does nothing.

void kfree(void* kptr) {
    if(kptr){
        pages[(uintptr_t) kptr / PAGESIZE].refcount -= 1;
        if(pages[(uintptr_t) kptr / PAGESIZE].refcount == 0)
            memset(kptr, 0xCC, PAGESIZE);
    }
}


// process_setup(pid, program_name)
//    Load application program `program_name` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %rip and %rsp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, const char* program_name) {
    log_printf("Setting up process %d...\n", pid);
    init_process(&ptable[pid], 0);

    // initialize processs page table
    log_printf("Initializing page table...\n");
    x86_64_pagetable* proc_pt = (x86_64_pagetable*) kalloc(PAGESIZE);
    memset(proc_pt, 0, PAGESIZE);
    assert(proc_pt);
    ptable[pid].pagetable = proc_pt;

    //copy mappings from kernel_pagetable
    vmiter proc_it = vmiter(proc_pt, 0);
    for(vmiter k_it(kernel_pagetable);
        k_it.va() < PROC_START_ADDR;
        k_it += PAGESIZE){
            proc_it.map(k_it.pa(), k_it.perm());
            proc_it += PAGESIZE;
    }
    log_printf("Page table initialized\n");

    // load the program
    program_loader loader(program_name);

    // allocate and map all memory
    for (loader.reset(); loader.present(); ++loader) {
        bool isWritable = loader.writable();
        for(proc_it.find(round_down(loader.va(), PAGESIZE));
            proc_it.va() < loader.va() + loader.size();
            proc_it += PAGESIZE){
                void* pa = kalloc(PAGESIZE);
                memset(pa, 0, PAGESIZE);
                if(isWritable){
                    proc_it.map(pa, PTE_P | PTE_W | PTE_U);
                } else{
                    proc_it.map(pa, PTE_P | PTE_U);
                }
            }
    }

    // copy instructions and data into place
    for (loader.reset(); loader.present(); ++loader) {
        proc_it.find(loader.va());
        memset((void*) proc_it.pa(), 0, loader.size());
        memcpy((void*) proc_it.pa(), loader.data(), loader.data_size());
    }
    log_printf("Code and data loaded...\n");

    // mark entry point
    ptable[pid].regs.reg_rip = loader.entry();

    // allocate stack
    uintptr_t stack_addr = MEMSIZE_VIRTUAL - PAGESIZE;
    proc_it.find(stack_addr);
    void* stack_pa = kalloc(PAGESIZE);
    memset(stack_pa, 0, PAGESIZE);
    proc_it.map(stack_pa, PTE_P | PTE_W | PTE_U);
    ptable[pid].regs.reg_rsp = stack_addr + PAGESIZE;
    log_printf("Stack was allocated\n");

    // mark process as runnable
    ptable[pid].state = P_RUNNABLE;
    log_printf("Success\n");
}



// exception(regs)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `regs`.
//    The processor responds to an exception by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-exception.S). That code saves more registers on the kernel's stack,
//    then calls exception().
//
//    Note that hardware interrupts are disabled when the kernel is running.

void exception(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /* log_printf("proc %d: exception %d at rip %p\n",
                current->pid, regs->reg_intno, regs->reg_rip); */

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    if (regs->reg_intno != INT_PF || (regs->reg_errcode & PFERR_USER)) {
        memshow();
    }

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_intno) {

    case INT_IRQ + IRQ_TIMER:
        ++ticks;
        lapicstate::get().ack();
        schedule();
        break;                  /* will not be reached */

    case INT_PF: {
        // Analyze faulting address and access type.
        uintptr_t addr = rdcr2();
        const char* operation = regs->reg_errcode & PFERR_WRITE
                ? "write" : "read";
        const char* problem = regs->reg_errcode & PFERR_PRESENT
                ? "protection problem" : "missing page";

        if (!(regs->reg_errcode & PFERR_USER)) {
            panic("Kernel page fault for %p (%s %s, rip=%p)!\n",
                  addr, operation, problem, regs->reg_rip);
        }
        console_printf(CPOS(24, 0), 0x0C00,
                       "Process %d page fault for %p (%s %s, rip=%p)!\n",
                       current->pid, addr, operation, problem, regs->reg_rip);
        current->state = P_BROKEN;
        break;
    }

    default:
        panic("Unexpected exception %d!\n", regs->reg_intno);

    }


    // Return to the current process (or run something else).
    if (current->state == P_RUNNABLE) {
        run(current);
    } else {
        schedule();
    }
}


// syscall(regs)
//    System call handler.
//
//    The register values from system call time are stored in `regs`.
//    The return value, if any, is returned to the user process in `%rax`.
//
//    Note that hardware interrupts are disabled when the kernel is running.

int syscall_page_alloc(uintptr_t addr);

pid_t sys_fork();

void sys_exit();
void exit_proc(pid_t pid);

int map_check();



uintptr_t syscall(regstate* regs) {
    // Copy the saved registers into the `current` process descriptor.
    current->regs = *regs;
    regs = &current->regs;

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /* log_printf("proc %d: syscall %d at rip %p\n",
                  current->pid, regs->reg_rax, regs->reg_rip); */

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    memshow();

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();


    // Actually handle the exception.
    switch (regs->reg_rax) {

    case SYSCALL_PANIC:
        panic(nullptr);         // does not return

    case SYSCALL_GETPID:
        return current->pid;

    case SYSCALL_YIELD:
        current->regs.reg_rax = 0;
        schedule();             // does not return

    case SYSCALL_PAGE_ALLOC:
        return syscall_page_alloc(current->regs.reg_rdi);

    case SYSCALL_FORK:
        return sys_fork();

    case SYSCALL_EXIT:
        sys_exit();
        schedule();

    default:
        panic("Unexpected system call %ld!\n", regs->reg_rax);

    }

    panic("Should not get here!\n");
}

// map_check(vmiter in_it, uintptr_t pa, int perm, pid_t pid)
// Attempts to map page address and permissions to given proccess
// if this fails, it exits the process cleanly.
int map_check(vmiter in_it, uintptr_t pa, int perm, pid_t pid){
    int res = in_it.try_map(pa, perm);
    if(res == -1){
        exit_proc(pid);
    }

    return res;
}

// syscall_page_alloc(addr)
//    Handles the SYSCALL_PAGE_ALLOC system call. This function
//    should implement the specification for `sys_page_alloc`
//    in `u-lib.hh` (but in the handout code, it does not).

int syscall_page_alloc(uintptr_t addr) {
    if(addr < PROC_START_ADDR || addr >= MEMSIZE_VIRTUAL || addr % PAGESIZE != 0){
        return -1;
    }
    void* alloc_pa = kalloc(PAGESIZE);
    if(!alloc_pa){
        return -1;
    }

    memset(alloc_pa, 0, PAGESIZE);
    x86_64_pagetable* process_pt = ptable[current->pid].pagetable;
    vmiter proc_it = vmiter(process_pt, addr);
    proc_it.map(alloc_pa, PTE_P | PTE_W | PTE_U);

    return 0;
}

// fork
// starts a new process as a copy of an existing process
// returns 0 to the child process, process id of child to parent

pid_t sys_fork()
{
    pid_t pid = 1;
    while(pid < NPROC && ptable[pid].state != P_FREE){
        ++pid;
    }
    if(pid == NPROC){
        return -1;
    }

    init_process(&ptable[pid], 0);

    x86_64_pagetable* child_pt = (x86_64_pagetable*) kalloc(PAGESIZE);
    x86_64_pagetable* parent_pt = current->pagetable;
    ptable[pid].pagetable = child_pt;

    if(!child_pt){
        return -1;
    }

    memset(child_pt, 0, PAGESIZE);
    vmiter child_it = vmiter(child_pt);

    for(vmiter k_it(kernel_pagetable);
        k_it.va() < PROC_START_ADDR;
        k_it += PAGESIZE) {
            if(map_check(child_it, k_it.pa(), k_it.perm(), pid)){
                return -1;
            }
            child_it += PAGESIZE;
        }

    child_it = vmiter(child_pt, PROC_START_ADDR);
    for(vmiter parent_it = vmiter(parent_pt, PROC_START_ADDR);
        parent_it.va() < MEMSIZE_VIRTUAL;
        parent_it += PAGESIZE, child_it += PAGESIZE){

            if(parent_it.writable() && parent_it.user()){
                void* child_pa = kalloc(PAGESIZE);
                if(!child_pa){
                    exit_proc(pid);
                    return -1;
                }
                memcpy(child_pa, (void*) parent_it.pa(), PAGESIZE);
                if(map_check(child_it, (uintptr_t) child_pa, parent_it.perm(), pid)){
                    kfree(child_pa);
                    return -1;
                }
            } else if(parent_it.present()){
                if(map_check(child_it, parent_it.pa(), parent_it.perm(), pid)){
                    return -1;
                }
                pages[parent_it.pa() / PAGESIZE].refcount += 1;
            }
    }

    ptable[pid].regs = current->regs;
    ptable[pid].regs.reg_rax = 0;

    ptable[pid].state = P_RUNNABLE;

    return pid;

}

void exit_proc(pid_t pid){
    x86_64_pagetable* proc_pt = ptable[pid].pagetable;

    if(proc_pt) {
        for(vmiter it(proc_pt, PROC_START_ADDR);
            it.va() <= MEMSIZE_VIRTUAL;
            it += PAGESIZE){
                if(it.present())
                    kfree((void*) it.pa());
            }
        for(ptiter it(proc_pt); it.active(); it.next()){
            kfree((void*) it.pa());
        }
        kfree((void*) proc_pt);
    }
    ptable[pid].state = P_FREE;
}

void sys_exit(){
    exit_proc(current -> pid);
}
// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule() {
    pid_t pid = current->pid;
    for (unsigned spins = 1; true; ++spins) {
        pid = (pid + 1) % NPROC;
        if (ptable[pid].state == P_RUNNABLE) {
            run(&ptable[pid]);
        }

        // If Control-C was typed, exit the virtual machine.
        check_keyboard();

        // If spinning forever, show the memviewer.
        if (spins % (1 << 12) == 0) {
            memshow();
            log_printf("%u\n", spins);
        }
    }
}


// run(p)
//    Run process `p`. This involves setting `current = p` and calling
//    `exception_return` to restore its page table and registers.

void run(proc* p) {
    assert(p->state == P_RUNNABLE);
    current = p;

    // Check the process's current pagetable.
    check_pagetable(p->pagetable);

    // This function is defined in k-exception.S. It restores the process's
    // registers then jumps back to user mode.
    exception_return(p);

    // should never get here
    while (true) {
    }
}


// memshow()
//    Draw a picture of memory (physical and virtual) on the CGA console.
//    Switches to a new process's virtual memory map every 0.25 sec.
//    Uses `console_memviewer()`, a function defined in `k-memviewer.cc`.

void memshow() {
    static unsigned last_ticks = 0;
    static int showing = 0;

    // switch to a new process every 0.25 sec
    if (last_ticks == 0 || ticks - last_ticks >= HZ / 2) {
        last_ticks = ticks;
        showing = (showing + 1) % NPROC;
    }

    proc* p = nullptr;
    for (int search = 0; !p && search < NPROC; ++search) {
        if (ptable[showing].state != P_FREE
            && ptable[showing].pagetable) {
            p = &ptable[showing];
        } else {
            showing = (showing + 1) % NPROC;
        }
    }

    extern void console_memviewer(proc* vmp);
    console_memviewer(p);
}
