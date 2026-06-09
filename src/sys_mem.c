/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "sched.h"
#include "queue.h"
#include <stdlib.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   if (krnl == NULL || regs == NULL)
      return -1;

   int memop = regs->a1;
   BYTE value;

   struct pcb_t *caller = sched_find_proc_by_pid(krnl, pid);
   if (caller == NULL)
      return -1;

   switch (memop) {
   case SYSMEM_MAP_OP:
            if (caller->mm == NULL)
               return -1;
            vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            if (caller->mm == NULL)
               return -1;
            if (inc_vma_limit(caller, regs->a2, regs->a3) != 0)
               return -1;
            break;
   case SYSMEM_SWP_OP:
            if (krnl->mram == NULL || krnl->active_mswp == NULL)
               return -1;
            __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            if (krnl->mram == NULL)
               return -1;
            if (MEMPHY_read(krnl->mram, regs->a2, &value) != 0)
               return -1;
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            if (krnl->mram == NULL)
               return -1;
            if (MEMPHY_write(krnl->mram, regs->a2, regs->a3) != 0)
               return -1;
            break;
   default:
            return -1;
   }

   return 0;
}


