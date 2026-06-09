/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include <string.h>
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL || mm->mmap == NULL)
    return NULL;

  struct vm_area_struct *pvma = mm->mmap;

  while (pvma != NULL && (int)pvma->vm_id < vmaid)
    pvma = pvma->vm_next;

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  return NULL;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  if (vmastart >= vmaend)
    return -1;

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
      return -1;
    vma = vma->vm_next;
  }

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 * Two cases:
 *   Case 1: sbrk + inc_sz <= vm_end  --> space already within VMA, just advance sbrk
 *   Case 2: sbrk + inc_sz >  vm_end  --> VMA must grow; expansion must be page-aligned
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || inc_sz == 0)
    return -1;

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  addr_t old_sbrk = cur_vma->sbrk;

  /* ---------------------------------------------------------------
   * Case 1: The requested region fits inside the already-mapped VMA.
   *         Simply advance sbrk by inc_sz and register the range
   *         [old_sbrk, new_sbrk] as a new free region.
   * --------------------------------------------------------------- */
  if (old_sbrk + inc_sz <= cur_vma->vm_end)
  {
    struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
    if (newrg == NULL)
      return -1;

    cur_vma->sbrk = old_sbrk + inc_sz;

    newrg->rg_start = old_sbrk;
    newrg->rg_end   = cur_vma->sbrk;   /* old_sbrk + inc_sz */
    newrg->vmaid    = vmaid;
    newrg->rg_next  = NULL;

    enlist_vm_rg_node(&cur_vma->vm_freerg_list, newrg);
    return 0;
  }

  /* ---------------------------------------------------------------
   * Case 2: The requested region exceeds the current vm_end.
   *         Align inc_sz up to the next page boundary so the VMA
   *         always grows in whole pages, then validate that the
   *         expanded range does not collide with another VMA.
   * --------------------------------------------------------------- */
#ifdef MM64
  addr_t aligned = PAGING64_PAGE_ALIGNSZ(inc_sz);
#else
  addr_t aligned = PAGING_PAGE_ALIGNSZ(inc_sz);
#endif
  if (aligned == 0)
    return -1;

  addr_t new_end = old_sbrk + aligned;   /* sbrk advances by aligned amount */

  /* Reject the expansion if it would overlap a neighbour VMA */
  if (validate_overlap_vm_area(caller, vmaid, cur_vma->vm_start, new_end) < 0)
    return -1;

  /* Register the entire new range as a free region */
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL)
    return -1;

  /* Commit the expansion */
  cur_vma->vm_end = new_end;
  cur_vma->sbrk   = new_end;

  newrg->rg_start = old_sbrk;
  newrg->rg_end   = cur_vma->sbrk;   /* old_sbrk + aligned */
  newrg->vmaid    = vmaid;
  newrg->rg_next  = NULL;

  enlist_vm_rg_node(&cur_vma->vm_freerg_list, newrg);
  return 0;
}

// #endif