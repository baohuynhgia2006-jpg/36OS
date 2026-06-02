/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

// START OF COMMON FUNCTIONS

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,       // present
             addr_t fpn,    // FPN
             int drt,       // dirty
             int swp,       // swap
             int swptyp,    // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0)
  {
    if (swp == 0)
    { // Non swap ~ page online
      if (fpn == 0)
        return -1; // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  /* Extract indices for each page-directory level (LA57-like 5-level) */
  *pgd = PAGING64_ADDR_PGD(addr);
  *p4d = PAGING64_ADDR_P4D(addr);
  *pud = PAGING64_ADDR_PUD(addr);
  *pmd = PAGING64_ADDR_PMD(addr);
  *pt = PAGING64_ADDR_PT(addr);

  /* In this simplified model the indices are returned to caller.
   * Actual mapping (walking/mm arrays) is handled by pte accessors. */

  return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                             pgd, p4d, pud, pmd, pt);
}

/*
 * walk_page_table - walk or create multi-level page tables and return pointer to PTE
 * @mm: target mm
 * @pgn: page number
 * @create: if non-zero, create intermediate levels as needed
 * returns: pointer to addr_t PTE entry or NULL if not present and create==0
 */
static addr_t *walk_page_table(struct mm_struct *mm, addr_t pgn, int create)
{
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  /* ensure top-level pgd exists */
  if (!mm->pgd)
    return NULL;

  /* PGD -> P4D */
  addr_t pgd_entry = mm->pgd[pgd_idx];
  addr_t *p4d = NULL;
  if ((pgd_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    p4d = calloc(512, sizeof(addr_t));
    if (!p4d)
      return NULL;
    mm->pgd[pgd_idx] = ((addr_t)(uintptr_t)p4d) | 1ULL;
  }
  else
  {
    p4d = (addr_t *)(uintptr_t)(pgd_entry & ~1ULL);
  }

  /* P4D -> PUD */
  addr_t p4d_entry = p4d[p4d_idx];
  addr_t *pud = NULL;
  if ((p4d_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pud = calloc(512, sizeof(addr_t));
    if (!pud)
      return NULL;
    p4d[p4d_idx] = ((addr_t)(uintptr_t)pud) | 1ULL;
  }
  else
  {
    pud = (addr_t *)(uintptr_t)(p4d_entry & ~1ULL);
  }

  /* PUD -> PMD */
  addr_t pud_entry = pud[pud_idx];
  addr_t *pmd = NULL;
  if ((pud_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pmd = calloc(512, sizeof(addr_t));
    if (!pmd)
      return NULL;
    pud[pud_idx] = ((addr_t)(uintptr_t)pmd) | 1ULL;
  }
  else
  {
    pmd = (addr_t *)(uintptr_t)(pud_entry & ~1ULL);
  }

  /* PMD -> PT */
  addr_t pmd_entry = pmd[pmd_idx];
  addr_t *pt = NULL;
  if ((pmd_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pt = calloc(512, sizeof(addr_t));
    if (!pt)
      return NULL;
    pmd[pmd_idx] = ((addr_t)(uintptr_t)pt) | 1ULL;
  }
  else
  {
    pt = (addr_t *)(uintptr_t)(pmd_entry & ~1ULL);
  }

  /* Return pointer to final PTE entry */
  return &pt[pt_idx];
}

// END OF COMMON FUNCTIONS

// START OF USER SPACE FUNCTIONS

/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte = walk_page_table(mm, pgn, 1);
  if (!pte)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte = walk_page_table(mm, pgn, 1);
  if (!pte)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte = walk_page_table(mm, pgn, 0);
  if (!pte)
    return 0;
  return (uint32_t)(*pte);
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte = walk_page_table(mm, pgn, 1);
  if (!pte)
    return -1;
  *pte = (addr_t)pte_val;
  return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller, // process call
                    addr_t addr,          // start address which is aligned to pagesz
                    int pgnum)            // num of mapping page
{
  struct mm_struct *mm = caller->mm;
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  int i;
  for (i = 0; i < pgnum; i++)
  {
    addr_t *pte = walk_page_table(mm, start_pgn + i, 1);
    if (pte)
      *pte = 0;
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                       addr_t addr,                    // start address which is aligned to pagesz
                       int pgnum,                      // num of mapping page
                       struct framephy_struct *frames, // list of the mapped frames
                       struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                      // no guarantee all given pages are mapped
  // struct framephy_struct *fpit;
  int i;
  struct mm_struct *mm = caller->mm;
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  struct framephy_struct *f = frames;

  /* update returned region */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + (addr_t)pgnum * PAGING64_PAGESZ;

  for (i = 0; i < pgnum; i++)
  {
    if (f == NULL)
      break;
    pte_set_fpn(caller, start_pgn + i, f->fpn);
    enlist_pgn_node(&mm->fifo_pgn, start_pgn + i);
    f = f->fp_next;
  }

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *head = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    if (MEMPHY_get_freefp(caller->mram, &fpn) == 0)
    {
      newfp_str = malloc(sizeof(struct framephy_struct));
      newfp_str->fpn = fpn;
      newfp_str->owner = caller->mm;
      newfp_str->fp_next = head;
      head = newfp_str;
    }
    else
    { /* Not enough free frames: cleanup and return error */
      struct framephy_struct *t = head;
      while (t)
      {
        struct framephy_struct *n = t->fp_next;
        MEMPHY_put_freefp(caller->mram, t->fpn);
        free(t);
        t = n;
      }
      *frm_lst = NULL;
      return -3000;
    }
  }

  *frm_lst = head;
  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_rg->rg_start = mapstart;
	ret_rg->rg_end = mapstart + (incpgnum * PAGING64_PAGESZ);

	for (int pgit = 0; pgit < incpgnum; pgit++) {
		addr_t current_vaddr = mapstart + (pgit * PAGING64_PAGESZ);
		addr_t pgn = PAGING64_PGN(current_vaddr);
		addr_t *pte = walk_page_table(caller->mm, pgn, 1);
		if (pte) *pte = 0; // Initialize as Not Present
	}
	return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* Initialize per-mm page directory storage. We use a flat PGD table
   * indexed by page-number for this simplified LA57 model. */
  mm->pgd = malloc(sizeof(addr_t) * PAGING64_MAX_PGN);
  if (mm->pgd)
  {
    int i;
    for (i = 0; i < PAGING64_MAX_PGN; i++)
      mm->pgd[i] = 0;
  }
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* TODO update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm;

  /* TODO: update mmap */
  mm->mmap = vma0;
  /* initialize symbol region table entries to invalid */
  {
    int i;
    for (i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
      mm->symrgtbl[i].vmaid = -1;
      mm->symrgtbl[i].rg_start = 0;
      mm->symrgtbl[i].rg_end = 0;
      mm->symrgtbl[i].rg_next = NULL;
    }
  }
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

// END OF USER-SPACE FUNCTION

// START OF KERNEL-SPACE FUNCTIONS

/*
 * walk_kernel_page_table - walk or create kernel page tables and return pointer to PTE
 * @krnl: kernel structure
 * @pgn: page number
 * @create: if non-zero, create intermediate levels as needed
 * returns: pointer to addr_t PTE entry or NULL if not present and create==0
 */
static addr_t *walk_kernel_page_table(struct krnl_t *krnl, addr_t pgn, int create)
{
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  /* ensure top-level pgd exists */
  if (!krnl || !krnl->krnl_pgd)
    return NULL;

  /* PGD -> P4D */
  addr_t pgd_entry = krnl->krnl_pgd[pgd_idx];
  addr_t *p4d = NULL;
  if ((pgd_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    p4d = calloc(512, sizeof(addr_t));
    if (!p4d)
      return NULL;
    krnl->krnl_pgd[pgd_idx] = ((addr_t)(uintptr_t)p4d) | 1ULL;
  }
  else
  {
    p4d = (addr_t *)(uintptr_t)(pgd_entry & ~1ULL);
  }

  /* P4D -> PUD */
  addr_t p4d_entry = p4d[p4d_idx];
  addr_t *pud = NULL;
  if ((p4d_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pud = calloc(512, sizeof(addr_t));
    if (!pud)
      return NULL;
    p4d[p4d_idx] = ((addr_t)(uintptr_t)pud) | 1ULL;
  }
  else
  {
    pud = (addr_t *)(uintptr_t)(p4d_entry & ~1ULL);
  }

  /* PUD -> PMD */
  addr_t pud_entry = pud[pud_idx];
  addr_t *pmd = NULL;
  if ((pud_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pmd = calloc(512, sizeof(addr_t));
    if (!pmd)
      return NULL;
    pud[pud_idx] = ((addr_t)(uintptr_t)pmd) | 1ULL;
  }
  else
  {
    pmd = (addr_t *)(uintptr_t)(pud_entry & ~1ULL);
  }

  /* PMD -> PT */
  addr_t pmd_entry = pmd[pmd_idx];
  addr_t *pt = NULL;
  if ((pmd_entry & 1ULL) == 0ULL)
  {
    if (!create)
      return NULL;
    pt = calloc(512, sizeof(addr_t));
    if (!pt)
      return NULL;
    pmd[pmd_idx] = ((addr_t)(uintptr_t)pt) | 1ULL;
  }
  else
  {
    pt = (addr_t *)(uintptr_t)(pmd_entry & ~1ULL);
  }

  /* Return pointer to final PTE entry */
  return &pt[pt_idx];
}

/*
 * krnl_pte_set_swap - Set PTE entry for swapped page in kernel space
 * @krnl: kernel structure
 * @pgn: page number
 * @swptyp: swap type
 * @swpoff: swap offset
 */
int krnl_pte_set_swap(struct krnl_t *krnl, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte = walk_kernel_page_table(krnl, pgn, 1);
  if (!pte)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * krnl_pte_set_fpn - Set PTE entry for on-line page in kernel space
 * @krnl: kernel structure
 * @pgn: page number
 * @fpn: frame page number (FPN)
 */
int krnl_pte_set_fpn(struct krnl_t *krnl, addr_t pgn, addr_t fpn)
{
  addr_t *pte = walk_kernel_page_table(krnl, pgn, 1);
  if (!pte)
    return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}

/*
 * krnl_pte_get_entry - Get PTE page table entry in kernel space
 * @krnl: kernel structure
 * @pgn: page number
 * returns: page table entry
 */
uint32_t krnl_pte_get_entry(struct krnl_t *krnl, addr_t pgn)
{
  addr_t *pte = walk_kernel_page_table(krnl, pgn, 0);
  if (!pte)
    return 0;
  return (uint32_t)(*pte);
}

/*
 * krnl_pte_set_entry - Set PTE page table entry in kernel space
 * @krnl: kernel structure
 * @pgn: page number
 * @pte_val: page table entry value
 */
int krnl_pte_set_entry(struct krnl_t *krnl, addr_t pgn, uint32_t pte_val)
{
  addr_t *pte = walk_kernel_page_table(krnl, pgn, 1);
  if (!pte)
    return -1;
  *pte = (addr_t)pte_val;
  return 0;
}

/*
 * krnl_vmap_pgd_memset - Clear a range of kernel page table entries
 * @krnl: kernel structure
 * @addr: start address which is aligned to pagesz
 * @pgnum: num of mapping pages
 */
int krnl_vmap_pgd_memset(struct krnl_t *krnl, addr_t addr, int pgnum)
{
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  int i;
  for (i = 0; i < pgnum; i++)
  {
    addr_t *pte = walk_kernel_page_table(krnl, start_pgn + i, 1);
    if (pte)
      *pte = 0;
  }

  return 0;
}

/*
 * krnl_vmap_page_range - map a range of kernel pages at aligned address
 * @krnl: kernel structure
 * @addr: start address which is aligned to pagesz
 * @pgnum: num of mapping page
 * @frames: list of the mapped frames
 * @ret_rg: return mapped region
 */
addr_t krnl_vmap_page_range(struct krnl_t *krnl, addr_t addr, int pgnum,
                            struct framephy_struct *frames, struct vm_rg_struct *ret_rg)
{
  int i;
  addr_t start_pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  struct framephy_struct *f = frames;

  /* update returned region */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + (addr_t)pgnum * PAGING64_PAGESZ;

  for (i = 0; i < pgnum; i++)
  {
    if (f == NULL)
      break;
    krnl_pte_set_fpn(krnl, start_pgn + i, f->fpn);
    f = f->fp_next;
  }

  return 0;
}

/*
 * krnl_alloc_pages_range - allocate req_pgnum of frames for kernel
 * @krnl: kernel structure
 * @mram: memory RAM device
 * @req_pgnum: request page num
 * @frm_lst: frame list
 */
addr_t krnl_alloc_pages_range(struct krnl_t *krnl, struct memphy_struct *mram, 
                              int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *head = NULL;

  if (!krnl || !mram)
    return -1;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    if (MEMPHY_get_freefp(mram, &fpn) == 0)
    {
      newfp_str = malloc(sizeof(struct framephy_struct));
      newfp_str->fpn = fpn;
      newfp_str->owner = NULL;  /* Kernel space, no mm owner */
      newfp_str->fp_next = head;
      head = newfp_str;
    }
    else
    { /* Not enough free frames: cleanup and return error */
      struct framephy_struct *t = head;
      while (t)
      {
        struct framephy_struct *n = t->fp_next;
        MEMPHY_put_freefp(mram, t->fpn);
        free(t);
        t = n;
      }
      *frm_lst = NULL;
      return -3000;
    }
  }

  *frm_lst = head;
  return 0;
}

/*
 * krnl_vm_map_ram - map kernel pages to ram storage device
 * @krnl: kernel structure
 * @mram: memory RAM device
 * @mapstart: start mapping point
 * @incpgnum: number of mapped pages
 * @ret_rg: returned region
 */
addr_t krnl_vm_map_ram(struct krnl_t *krnl, struct memphy_struct *mram,
                       addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;

  ret_alloc = krnl_alloc_pages_range(krnl, mram, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* Map the allocated frames to kernel space */
  if (krnl_vmap_page_range(krnl, mapstart, incpgnum, frm_lst, ret_rg) < 0)
  {
    /* cleanup allocated frames list on failure */
    struct framephy_struct *t = frm_lst;
    while (t)
    {
      struct framephy_struct *n = t->fp_next;
      MEMPHY_put_freefp(mram, t->fpn);
      free(t);
      t = n;
    }
    return -1;
  }

  return 0;
}

/*
 * init_kernel_mm - Initialize kernel memory management structures
 * @krnl: kernel structure to initialize
 * allocates and initializes the kernel page tables
 */
int init_kernel_mm(struct krnl_t *krnl)
{
  if (!krnl)
    return -1;

  /* Initialize kernel page directory storage */
  krnl->krnl_pgd = malloc(sizeof(addr_t) * PAGING64_MAX_PGN);
  if (!krnl->krnl_pgd)
    return -1;

  /* Zero out all PGD entries */
  int i;
  for (i = 0; i < PAGING64_MAX_PGN; i++)
    krnl->krnl_pgd[i] = 0;

  /* Initialize other level pointers to NULL (will be allocated on demand) */
  krnl->krnl_p4d = NULL;
  krnl->krnl_pud = NULL;
  krnl->krnl_pmd = NULL;
  krnl->krnl_pt = NULL;

  return 0;
}

// END OF KERNEL-SPACE FUNCTIONS

// START OF COMMON FUNCTIONS

/*
 * __swap_cp_page - Copy a full page of data between memory devices
 * @mpsrc  : Source physical memory device (e.g., RAM or Swap)
 * @srcfpn : Source Frame Physical Number (FPN)
 * @mpdst  : Destination physical memory device
 * @dstfpn : Destination Frame Physical Number (FPN)
 *
 * Reads byte-by-byte from the source frame and writes to the destination frame.
 * Typically used for moving pages between active RAM and Swap storage.
 */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
				   struct memphy_struct *mpdst, addr_t dstfpn)
{
	int cellidx;
	addr_t addrsrc, addrdst;
	
	int pagesz = PAGING64_PAGESZ;


	for (cellidx = 0; cellidx < pagesz; cellidx++)
	{
		addrsrc = srcfpn * pagesz + cellidx;
		addrdst = dstfpn * pagesz + cellidx;

		BYTE data;
		MEMPHY_read(mpsrc, addrsrc, &data);
		MEMPHY_write(mpdst, addrdst, data);
	}

	return 0;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL)
  {
    printf("NULL list\n");
    return -1;
  }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  // addr_t pgn_start;//, pgn_end;
  // addr_t pgit;
  // struct krnl_t *krnl = caller->krnl;

  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */

  return 0;
}

// END OF COMMON FUNCTIONS

#endif // def MM64
