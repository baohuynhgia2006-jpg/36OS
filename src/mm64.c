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
#include <string.h>

#if defined(MM64)

#define PAGING64_TABLE_ENTRIES (1 << (PAGING64_ADDR_PGD_HIBIT - PAGING64_ADDR_PGD_LOBIT + 1))
#define PAGING64_PGN(x)         ((x) >> PAGING64_ADDR_PT_SHIFT)
#define PAGING64_OFFST(x)       ((x) & (PAGING64_PAGESZ - 1))

static addr_t *alloc_pt_level(void)
{
  return calloc(PAGING64_TABLE_ENTRIES, sizeof(addr_t));
}

static int page_table_walk(struct mm_struct *mm, addr_t pgn, addr_t **entry, int allocate)
{
  if (mm == NULL || mm->pgd == NULL)
    return -1;

  addr_t addr = pgn << PAGING64_ADDR_PT_SHIFT;
  uint32_t idx[5];
  idx[0] = PAGING64_ADDR_PGD(addr);
  idx[1] = PAGING64_ADDR_P4D(addr);
  idx[2] = PAGING64_ADDR_PUD(addr);
  idx[3] = PAGING64_ADDR_PMD(addr);
  idx[4] = PAGING64_ADDR_PT(addr);

  addr_t *table = mm->pgd;
  for (int level = 0; level < 4; level++)
  {
    addr_t ptr = table[idx[level]];
    if (ptr == 0)
    {
      if (!allocate)
        return -1;
      addr_t *next = alloc_pt_level();
      if (next == NULL)
        return -1;
      table[idx[level]] = (addr_t)(uintptr_t)next;
      ptr = table[idx[level]];
    }
    table = (addr_t *)(uintptr_t)ptr;
  }

  *entry = &table[idx[4]];
  return 0;
}

int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  *pte = 0;
  if (pre != 0)
  {
    if (swp == 0)
    {
      if (fpn == 0)
        return -1;

      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    {
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}

int get_pd_from_address(addr_t addr, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  *pgd = PAGING64_ADDR_PGD(addr);
  *p4d = PAGING64_ADDR_P4D(addr);
  *pud = PAGING64_ADDR_PUD(addr);
  *pmd = PAGING64_ADDR_PMD(addr);
  *pt  = PAGING64_ADDR_PT(addr);
  return 0;
}

int get_pd_from_pagenum(addr_t pgn, addr_t *pgd, addr_t *p4d, addr_t *pud, addr_t *pmd, addr_t *pt)
{
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                             pgd, p4d, pud, pmd, pt);
}

int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *entry;
  if (page_table_walk(caller->krnl->mm, pgn, &entry, 1) != 0)
    return -1;

  addr_t pte = 0;
  init_pte(&pte, 1, 0, 0, 1, swptyp, swpoff);
  *entry = pte;
  return 0;
}

int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  addr_t *entry;
  if (page_table_walk(caller->krnl->mm, pgn, &entry, 1) != 0)
    return -1;

  addr_t pte = 0;
  init_pte(&pte, 1, fpn, 0, 0, 0, 0);
  *entry = pte;
  return 0;
}

uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  addr_t *entry;
  if (page_table_walk(caller->krnl->mm, pgn, &entry, 0) != 0)
    return 0;
  return (uint32_t)(*entry & 0xFFFFFFFFUL);
}

int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  addr_t *entry;
  if (page_table_walk(caller->krnl->mm, pgn, &entry, 1) != 0)
    return -1;
  *entry = (addr_t)pte_val;
  return 0;
}

int vmap_pgd_memset(struct pcb_t *caller,
                    addr_t addr,
                    int pgnum)
{
  if (caller == NULL || pgnum <= 0)
    return -1;

  addr_t start_pgn = PAGING64_PGN(addr);
  for (int i = 0; i < pgnum; i++)
  {
    addr_t *dummy;
    if (page_table_walk(caller->krnl->mm, start_pgn + i, &dummy, 1) != 0)
      return -1;
  }
  return 0;
}

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  if (caller == NULL || req_pgnum <= 0 || frm_lst == NULL)
    return -1;

  struct framephy_struct *head = NULL;
  struct framephy_struct *tail = NULL;

  for (int i = 0; i < req_pgnum; i++)
  {
    addr_t fpn;
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == -1)
    {
      while (head != NULL)
      {
        struct framephy_struct *next = head->fp_next;
        MEMPHY_put_freefp(caller->krnl->mram, head->fpn);
        free(head);
        head = next;
      }
      return -1;
    }

    struct framephy_struct *node = malloc(sizeof(struct framephy_struct));
    if (node == NULL)
    {
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
      while (head != NULL)
      {
        struct framephy_struct *next = head->fp_next;
        MEMPHY_put_freefp(caller->krnl->mram, head->fpn);
        free(head);
        head = next;
      }
      return -1;
    }

    node->fpn = fpn;
    node->fp_next = NULL;
    if (tail != NULL)
      tail->fp_next = node;
    else
      head = node;
    tail = node;
  }

  *frm_lst = head;
  return 0;
}

addr_t vmap_page_range(struct pcb_t *caller,
                    addr_t addr,
                    int pgnum,
                    struct framephy_struct *frames,
                    struct vm_rg_struct *ret_rg)
{
  if (caller == NULL || pgnum <= 0)
    return -1;

  struct framephy_struct *frame = frames;
  if (frame == NULL)
  {
    if (alloc_pages_range(caller, pgnum, &frame) != 0)
      return -1;
  }

  addr_t current = addr;
  for (int i = 0; i < pgnum; i++)
  {
    if (frame == NULL)
      return -1;

    pte_set_fpn(caller, PAGING64_PGN(current), frame->fpn);
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, PAGING64_PGN(current));
    current += PAGING64_PAGESZ;
    frame = frame->fp_next;
  }

  if (ret_rg != NULL)
  {
    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + (addr_t)pgnum * PAGING64_PAGESZ;
    ret_rg->vmaid = 0;
    ret_rg->rg_next = NULL;
    ret_rg->rg_prev = NULL;
  }

  return addr;
}

addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  if (incpgnum <= 0)
    return -1;

  if (alloc_pages_range(caller, incpgnum, &frm_lst) != 0)
    return -1;

  if (vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg) == (addr_t)-1)
    return -1;

  return 0;
}

int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  if (mm == NULL)
    return -1;

  mm->pgd = alloc_pt_level();
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;
  memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));

  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  if (vma0 == NULL)
    return -1;

  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = 0;
  vma0->sbrk = 0;
  vma0->vm_mm = mm;
  vma0->vm_freerg_list = NULL;
  vma0->vm_next = NULL;
  vma0->vm_prev = NULL;

  mm->mmap = vma0;

  if (caller != NULL)
    caller->mm = mm;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));
  if (rgnode == NULL)
    return NULL;

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;
  rgnode->rg_prev = NULL;
  rgnode->vmaid = 0;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  if (rglist == NULL || rgnode == NULL)
    return -1;

  rgnode->rg_next = *rglist;
  if (*rglist != NULL)
    (*rglist)->rg_prev = rgnode;

  *rglist = rgnode;
  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));
  if (pnode == NULL)
    return -1;

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  pnode->pg_prev = NULL;
  if (*plist != NULL)
    (*plist)->pg_prev = pnode;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
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
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
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
  if (ip == NULL) { printf("NULL list\n"); return -1; }
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
//addr_t pgn_start;//, pgn_end;
//addr_t pgit;
//struct krnl_t *krnl = caller->krnl;

  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);

  /* TODO traverse the page map and dump the page directory entries */

  return 0;
}

#endif  //def MM64

