#include <elf.h>

#include "batch.h"
#include "trap.h"
#include "printk.h"

extern void _num_app(void);

AppManager app_manager;
void init_app_manager(void) {
    const uint64_t *ptr = (const uint64_t *)_num_app;
    uint64_t num_apps = *ptr;

    app_manager.num_apps = num_apps;
    app_manager.current_app = 0;

    
    ptr++;
    for (int i = 0; i <= num_apps; i++) {
        app_manager.app_start[i] = *(ptr + i);
    }
}
/*
User Space Layout
0x0000 0000 0000 0000 - 0x0000 0000 0000 ffff Reserved
0x0000 0000 0001 0000 -                       User Program, defined in ELF
0x

*/
void load_app(Task *task, uint32_t appid) {
    if (appid > app_manager.num_apps) {
        printk("All applications are completed!\n");
        return;
    }
    
    vpn_t ptbase_vpn;

    pfn_t app_ptbase = uptalloc(&ptbase_vpn);
    task->ptbase_pfn = app_ptbase;
    task->ptbase_vpn = ptbase_vpn;

    PTReference *ptref_base = kalloc(2 * PAGESIZE);
    task->ptref_base = ptref_base;

    // map trampoline
    extern void strampoline();
    uptmap(ptbase_vpn, ptref_base, ADDR_2_PAGE(TRAMPOLINE), ADDR_2_PAGE(strampoline), PTE_VALID | PTE_READ | PTE_EXECUTE);

    Elf64_Ehdr *elf_header = (Elf64_Ehdr *)app_manager.app_start[appid];
    vpn_t max_vpn = 0;
    uint64_t num_sections = 0;
    for (int i = 0; i < elf_header->e_phnum; i++) {
        Elf64_Phdr *program_header = (Elf64_Phdr *)(app_manager.app_start[appid] + elf_header->e_phoff) + i;
        if (program_header->p_type != PT_LOAD) {
            continue;
        }
        uint64_t start_va = program_header->p_vaddr;
        uint64_t end_va = program_header->p_vaddr + program_header->p_memsz;
        uint64_t flags = PTE_USER | PTE_VALID;
        flags |= (program_header->p_flags & PF_R) ? PTE_READ : 0;
        flags |= (program_header->p_flags & PF_W) ? PTE_WRITE : 0;
        flags |= (program_header->p_flags & PF_X) ? PTE_EXECUTE : 0;
        void *program_start = (void *)(app_manager.app_start[appid] + program_header->p_offset);
        if (ADDR_2_PAGEUP(end_va) > max_vpn)
            max_vpn = ADDR_2_PAGEUP(end_va);
        for (vpn_t vpn = ADDR_2_PAGE(start_va); vpn < ADDR_2_PAGEUP(end_va); ++vpn) {
            vpn_t kernel_vpn;
            pfn_t pfn = uptalloc(&kernel_vpn);
            uint64_t offset = (uint64_t)PAGE_2_ADDR(vpn) - start_va;
            memcpy(PAGE_2_ADDR(kernel_vpn), (void *)((uint64_t)program_start + offset), PAGESIZE);
            uptmap(ptbase_vpn, ptref_base, vpn, pfn, flags);

            task->sections[num_sections].pfn = pfn;
            task->sections[num_sections].vpn = kernel_vpn;
            num_sections++;
        }
    }
    task->num_sections = num_sections;

    // map user stack;
    // guard page

    max_vpn += 1;
    for (vpn_t vpn = max_vpn; vpn < max_vpn + USER_STACK_SIZE / PAGESIZE; vpn++) {
        vpn_t kernel_vpn;
        pfn_t pfn = uptalloc(&kernel_vpn);
        uptmap(ptbase_vpn, ptref_base, vpn, pfn, PTE_USER | PTE_VALID | PTE_READ | PTE_WRITE);
        task->user_stack[vpn - max_vpn].pfn = pfn;
        task->user_stack[vpn - max_vpn].vpn = vpn;
    }
    // trap context
    vpn_t trap_context;
    pfn_t pfn = uptalloc(&trap_context);
    uptmap(ptbase_vpn, ptref_base, ADDR_2_PAGE(TRAPFRAME), pfn, PTE_VALID | PTE_READ | PTE_WRITE);
    task->trap_vpn = trap_context;
    task->trap_pfn = pfn;

    uint8_t *kernel_sp = kalloc(KERNEL_STACK_SIZE);
    task->kernel_stack = kernel_sp;
    kernel_sp += KERNEL_STACK_SIZE;

    app_init_context(PAGE_2_ADDR(trap_context), elf_header->e_entry, (uint64_t)PAGE_2_ADDR(max_vpn) + USER_STACK_SIZE, (uint64_t)kernel_sp);
}

void run_next_app(void) {
    extern void strampoline();
    extern void __restore();

    if (app_manager.current_app != 0) {
        // free stuff
        ptref_free(app_manager.current_task.ptbase_pfn, app_manager.current_task.ptbase_vpn, app_manager.current_task.ptref_base);
        for (uint64_t i = 0; i < app_manager.current_task.num_sections; i++) {
            uptfree(app_manager.current_task.sections[i].pfn, app_manager.current_task.sections[i].vpn);
        }
        for (uint64_t i = 0; i < USER_STACK_SIZE / PAGESIZE; i++) {
            uptfree(app_manager.current_task.user_stack[i].pfn, app_manager.current_task.user_stack[i].vpn);
        }
        uptfree(app_manager.current_task.trap_pfn, app_manager.current_task.trap_vpn);
        kfree(app_manager.current_task.kernel_stack, KERNEL_STACK_SIZE);

    }
    load_app(&app_manager.current_task, app_manager.current_app);
    app_manager.current_app++;
    
    uint64_t token = ((uint64_t)1 << 63) | app_manager.current_task.ptbase_pfn;

    void (*restore)(void *trap_context, uint64_t user_token) = (void (*)(void *, uint64_t))(TRAMPOLINE + (uint64_t)__restore - (uint64_t)strampoline);
    restore(TRAPFRAME, token);
    panic("Unreachable code!");
}