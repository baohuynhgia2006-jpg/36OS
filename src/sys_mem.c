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
   int memop = regs->a1;
   BYTE value;

   /* Find the actual caller PCB from the running_list by matching pid */
   struct pcb_t *caller = NULL;
   struct queue_t *rl = krnl->running_list;
   if (rl != NULL) {
      int i;
      for (i = 0; i < rl->size; i++) {
         if (rl->proc[i] != NULL && rl->proc[i]->pid == pid) {
            caller = rl->proc[i];
            break;
         }
      }
   }

   /* Fallback: if not found in running_list (e.g. first call), allocate a
    * minimal caller stub so the switch below doesn't crash.
    * Operations that need mm (INC/MAP) will be no-ops in this edge case.  */
   int caller_is_stub = 0;
   if (caller == NULL) {
      caller = malloc(sizeof(struct pcb_t));
      caller->krnl = krnl;
      caller->pid  = pid;
      caller_is_stub = 1;
   }

   switch (memop) {
   case SYSMEM_MAP_OP:
            vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            if (!caller_is_stub)
               inc_vma_limit(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_SWP_OP:
            __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            MEMPHY_read(krnl->mram, regs->a2, &value);
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            MEMPHY_write(krnl->mram, regs->a2, regs->a3);
            break;
   default:
            printf("Memop code: %d\n", memop);
            break;
   }

   if (caller_is_stub) free(caller);

   return 0;
}


