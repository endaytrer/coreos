#include "pagetable.h"
#include "printk.h"

pfn_t pfn_start;
pfn_t pt_base;
static pte_t *kpgdir = (pte_t *)0x1000;

FreeNode *freelist = NULL;
FreeNode *pfreelist = NULL;

// managed memory space.

/*
Kernel Memory Layout:
    0x0000 0000 0000 0000 - 0x0000 0000 7fff ffff     Page table recursive mapping
    0x0000 0000 8000 0000 - 0x0000 0000 801f ffff     (Reserved)
    0x0000 0000 8020 0000 - 0x0000 0000 ffff ffff     kernel sections
    0x0000 0001 0000 0000 - 0x0000 003f ffff ffff     unmanaged mapping space, with manual physical frame allocation, for managing user ptbases
    0xffff ffe0 0000 0000 - 0xffff ffff ffff efff     managed mapping space, cannot free physical frames once allocated
    0xffff ffff ffff f000 - 0xffff ffff ffff ffff     trampoline
*/

void *kbrk = (void *)0xffffffe000000000;

void *ptbrk = (void *)0x100000000;
void *pttop = (void *)0x4000000000;


// unmanaged memory space is from 0x0 to 0x100000000, using palloc

pfn_t palloc() {
    // Allocate one physical page to void. Use this for pagetables.
    if (pfreelist) {
        pfn_t pfn = pfreelist->type.pfn;
        FreeNode *next = pfreelist->next;
        ptunmap(ADDR_2_PAGE(pfreelist));
        pfreelist = next;
        return pfn;
    }
    return pfn_start++;
}

FreeNode *ptfreelist = NULL;
pfn_t uptalloc(vpn_t *out_vpn) {
    // allocate user page table
    if (ptfreelist) {
        // since every uptalloc have same flags, no need to unmap / remap
        pfn_t pfn = ptfreelist->type.pfn;
        FreeNode *next = ptfreelist->next;
        vpn_t vpn = ADDR_2_PAGE(ptfreelist);
        memset(ptfreelist, 0, sizeof(FreeNode));
        ptfreelist = next;
        *out_vpn = vpn;
        return pfn;
    }
    vpn_t vpn = ADDR_2_PAGE(ptbrk);
    pfn_t pfn = palloc_ptr(vpn, PTE_VALID | PTE_READ | PTE_WRITE);
    ptbrk += PAGESIZE;
    *out_vpn = vpn;
    return pfn;
}

void uptfree(pfn_t pfn, vpn_t vpn) {
    // free user page table
    FreeNode *temp = ptfreelist;
    ptfreelist = (FreeNode *)PAGE_2_ADDR(vpn);
    ptfreelist->type.pfn = pfn;
    ptfreelist->next = temp;
}

pfn_t palloc_ptr(vpn_t vpn, uint64_t flags) {
    // Allocate one physical page to virtual page vpn. One have to manage the space once allocated.
    if (pfreelist) {
        pfn_t pfn = pfreelist->type.pfn;
        FreeNode *next = pfreelist->next;
        ptunmap(ADDR_2_PAGE(pfreelist));
        pfreelist = next;
        ptmap(vpn, pfn, flags);
        return pfn;
    }
    pfn_t pfn = pfn_start++;
    ptmap(vpn, pfn, flags);

    memset(PAGE_2_ADDR(vpn), 0, PAGESIZE);

    return pfn;
}

void pfree(pfn_t pfn, vpn_t vpn) {
    FreeNode *temp = pfreelist;
    pfreelist = (FreeNode *)PAGE_2_ADDR(vpn);
    pfreelist->type.pfn = pfn;
    pfreelist->next = temp;
}

void *kalloc(uint64_t size) {
    uint64_t pages = ADDR_2_PAGEUP(size);

    FreeNode *p = freelist, *prev = NULL;
    while (p != NULL) {
        if (p->type.size > pages) {
            FreeNode *newp = (FreeNode *)((uint64_t)p + pages * PAGESIZE);
            newp->next = p->next;
            newp->type.size = (p->type.size - pages);
            prev->next = newp;
            memset(p, 0, sizeof(FreeNode));
            return p;
        } else if (p->type.size == pages) {
            prev->next = p->next;
            memset(p, 0, sizeof(FreeNode));
            return p;
        }
        prev = p;
        p = p->next;
    }
    void *ans = kbrk;
    for (uint64_t i = 0; i < pages; i++) {
        palloc_ptr(ADDR_2_PAGE(kbrk), PTE_VALID | PTE_READ | PTE_WRITE);
        // discarding pfn returned, thus unable to reallocate.
        kbrk = (void *)((uint64_t)kbrk + PAGESIZE);
    }
    memset(ans, 0, size);
    return ans;
}

void kfree(void *ptr, uint64_t size) {
    uint64_t pages = ADDR_2_PAGEUP(size);
    // lazy approach, leave fragments
    FreeNode *free_head = (FreeNode *)ptr;
    free_head->type.size = size;
    free_head->next = freelist;
    freelist = free_head;
}

void uptmap(vpn_t uptbase, PTReference *ptref_base, vpn_t vpn, pfn_t pfn, uint64_t flags) {
    // map in user page table
    // uiptbase is a page table, but using virtual addresses, to track lower level page tables

    pte_t *pte = (pte_t *)((uint64_t)PAGE_2_ADDR(uptbase) | (VPN(2, vpn) << 3));
    PTReference *ptref = ptref_base + VPN(2, vpn);

    if (!ptref->ptable) {
        vpn_t temp_vpn;
        pfn_t temp_pfn = uptalloc(&temp_vpn);
        PTReference *next_ptref = kalloc(2 * PAGESIZE);

        *pte = PTE(temp_pfn, PTE_USER | PTE_VALID);
        ptref->ptable = PAGE_2_ADDR(temp_vpn);
        ptref->pt_reference = next_ptref;
    }

    pte = (pte_t *)((uint64_t)ptref->ptable | (VPN(1, vpn) << 3));
    ptref = ptref->pt_reference + VPN(1, vpn);

    if (!ptref->ptable) {
        vpn_t temp_vpn;
        pfn_t temp_pfn = uptalloc(&temp_vpn);

        *pte = PTE(temp_pfn, PTE_USER | PTE_VALID);
        ptref->ptable = PAGE_2_ADDR(temp_vpn);
    }

    pte = (pte_t *)((uint64_t)ptref->ptable | (VPN(0, vpn) << 3));
    *pte = PTE(pfn, flags);
}

void uptunmap(vpn_t uptbase, PTReference *ptref_base, vpn_t vpn) {
    // unmap in user page table
    // uiptbase is a page table, but using virtual addresses, to track lower level page tables

    pte_t *pte = (pte_t *)((uint64_t)PAGE_2_ADDR(uptbase) | (VPN(2, vpn) << 3));
    PTReference *ptref = ptref_base + VPN(2, vpn);

    if (!ptref->ptable) {
        panic("Cannot unmap unmapped page\n");
    }

    pte = (pte_t *)((uint64_t)ptref->ptable | (VPN(1, vpn) << 3));
    ptref = ptref->pt_reference + VPN(1, vpn);

    if (!ptref->ptable) {
        panic("Cannot unmap unmapped page\n");
    }

    pte = (pte_t *)((uint64_t)ptref->ptable | (VPN(0, vpn) << 3));
    *pte = 0;
}
void ptref_free(pfn_t ptbase_pfn, vpn_t ptbase_vpn, PTReference *ptref_base) {
    for (PTReference *ptr = ptref_base; ptr - ptref_base < PAGESIZE / sizeof(pte_t); ++ptr) {
        if (ptr->ptable) {
            pte_t *pte = (pte_t *)((uint64_t)PAGE_2_ADDR(ptbase_vpn) | ((ptr - ptref_base) << 3));
            pfn_t ptable_pfn = GET_PFN(pte);
            ptref_free(ptable_pfn, ADDR_2_PAGE(ptr->ptable), ptr->pt_reference);
        }
    }
    uptfree(ptbase_pfn, ptbase_vpn);
    kfree(ptref_base, PAGESIZE * 2);
}

void ptmap(vpn_t vpn, pfn_t pfn, uint64_t flags) {
    // level 2
    pte_t *ptable = kpgdir;
    pte_t *pte = (pte_t *)(((uint64_t) ptable) | (VPN(2, vpn) << 3));

    if (!(*pte & PTE_VALID)) {
        pfn_t temp_pfn = palloc();
        // we have to temporally set r+w flags, since we are goint to modify its children.
        *pte = PTE(temp_pfn, PTE_VALID | PTE_READ | PTE_WRITE);
    } else {
        SET_FLAGS(pte, PTE_VALID | PTE_READ | PTE_WRITE);
    }

    // level 1
    ptable = (pte_t *)(VPN(2, vpn) << 12);
    pte_t *new_pte = (pte_t *)(((uint64_t) ptable) | (VPN(1, vpn) << 3));
    if (!(*new_pte & PTE_VALID)) {
        pfn_t temp_pfn = palloc();
        *new_pte = PTE(temp_pfn, PTE_VALID | PTE_READ | PTE_WRITE);
    } else {
        SET_FLAGS(new_pte, PTE_VALID | PTE_READ | PTE_WRITE);
    }
    SET_FLAGS(pte, PTE_VALID);
    pte = new_pte;

    // level 0
    ptable = (pte_t *)((VPN(2, vpn) << 21) | (VPN(1, vpn) << 12));
    new_pte = (pte_t *)(((uint64_t) ptable) | (VPN(0, vpn) << 3));
    *new_pte = PTE(pfn, flags);
    SET_FLAGS(pte, PTE_VALID);
}

void ptunmap(vpn_t vpn) {
    // level 2
    pte_t *ptable = kpgdir;
    pte_t *pte = (pte_t *)(((uint64_t) ptable) | (VPN(2, vpn) << 3));

    if (!(*pte & PTE_VALID)) {
        panic("Cannot unmap unmapped page\n");
    } else {
        SET_FLAGS(pte, PTE_VALID | PTE_READ | PTE_WRITE);
    }

    // level 1
    ptable = (pte_t *)(VPN(2, vpn) << 12);
    pte_t *new_pte = (pte_t *)(((uint64_t) ptable) | (VPN(1, vpn) << 3));
    if (!(*new_pte & PTE_VALID)) {
        panic("Cannot unmap unmapped page\n");
    } else {
        SET_FLAGS(pte, PTE_VALID | PTE_READ | PTE_WRITE);
    }
    SET_FLAGS(pte, PTE_VALID);
    pte = new_pte;

    // level 0
    ptable = (pte_t *)((VPN(2, vpn) << 21) | (VPN(1, vpn) << 12));
    new_pte = (pte_t *)(((uint64_t) ptable) | (VPN(0, vpn) << 3));
    SET_FLAGS(new_pte, 0);
    SET_FLAGS(pte, PTE_VALID);
}

void ptmap_physical(vpn_t vpn, pfn_t pfn, uint64_t flags) {
    pte_t *pte;
    pfn_t temp_pfn = pt_base;
    for (int level = 2; level > 0; level--) {
        pte = (pte_t *)((uint64_t)PAGE_2_ADDR(temp_pfn) | (VPN(level, vpn) << 3));
        if (!(*pte & PTE_VALID)) {
            temp_pfn = palloc();
            *pte = PTE(temp_pfn, PTE_VALID);
        } else {
            temp_pfn = GET_PFN(pte);
        }
    }
    pte = (pte_t *)((uint64_t)PAGE_2_ADDR(temp_pfn) | (VPN(0, vpn) << 3));
    *pte = PTE(pfn, flags);
}


void init_pagetable(void) {

    extern void stext();
    extern void strampoline();

    extern void etext();
    extern void srodata();
    extern void erodata();
    extern void sdata();
    extern void ebss();

    extern void ekernel();
    // map recursive page table
    pfn_start = ADDR_2_PAGE(ekernel);
    pt_base = palloc();

    // making the 0th entry pointing to itself (recursive mapping), and 1st entry pointing to it self with rw permission
    // in this way, first page = 0x000 000 000 xxx, does not have r/w permission
    // page directory = 0x000 000 001 000, with r/w permission, be specially careful about this.
    *(pte_t *)((uint64_t)PAGE_2_ADDR(pt_base) | 0x000) = PTE(pt_base, PTE_VALID);
    *(pte_t *)((uint64_t)PAGE_2_ADDR(pt_base) | 0x008) = PTE(pt_base, PTE_VALID | PTE_READ | PTE_WRITE);
    
    // map kernel code identically with r-x
    for (pfn_t pfn = ADDR_2_PAGE(stext); pfn < ADDR_2_PAGEUP(etext); ++pfn) {
        ptmap_physical(pfn, pfn, PTE_VALID | PTE_READ | PTE_EXECUTE);
    }
    // map kernel rodata identically with r--
    for (pfn_t pfn = ADDR_2_PAGE(srodata); pfn < ADDR_2_PAGEUP(erodata); ++pfn) {
        ptmap_physical(pfn, pfn, PTE_VALID | PTE_READ);
    }
    // map kernel data and bss identically with rw-
    for (pfn_t pfn = ADDR_2_PAGE(sdata); pfn < ADDR_2_PAGEUP(ebss); ++pfn) {
        ptmap_physical(pfn, pfn, PTE_VALID | PTE_READ | PTE_WRITE);
    }
    // map trampoline with r-x
    ptmap_physical(ADDR_2_PAGE(TRAMPOLINE), ADDR_2_PAGE(strampoline), PTE_VALID | PTE_READ | PTE_EXECUTE);

    // activate paging
    uint64_t token = ((uint64_t)1 << 63) | pt_base;
    pfn_t temp_pfn_start = pfn_start;
    asm volatile(
        "csrw satp, %0\n\t"
        "sfence.vma"
        :: "r" (token)
    );
    pfn_start = temp_pfn_start;
}