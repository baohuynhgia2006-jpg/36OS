/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
	struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

	if (rg_elmt->rg_start >= rg_elmt->rg_end)
		return -1;

	if (rg_node != NULL)
		rg_elmt->rg_next = rg_node;

	/* Enlist the new region */
	mm->mmap->vm_freerg_list = rg_elmt;

	return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
	if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
		return NULL;

	return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
	/*Allocate at the toproof */
	pthread_mutex_lock(&mmvm_lock);

	struct mm_struct * os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	struct vm_rg_struct rgnode;
	struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
	int inc_sz = 0;

	if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
	{
		caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
		caller->krnl->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

		*alloc_addr = rgnode.rg_start;

		caller->krnl->mm = os_mm;

		pthread_mutex_unlock(&mmvm_lock);
		return 0;
	}

	/* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

	/*Attempt to increate limit to get space */
#ifdef MM64
	inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ);
	inc_sz = inc_sz + 1;
#else
	inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
	int old_sbrk;
	inc_sz = inc_sz + 1;

	old_sbrk = cur_vma->sbrk;

	/* TODO INCREASE THE LIMIT
	 * SYSCALL 1 sys_memmap
	 */
	struct sc_regs regs;
	regs.a1 = SYSMEM_INC_OP;
	regs.a2 = vmaid;
#ifdef MM64
	regs.a3 = size;
#else
	regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
	_syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

	/*Successful increase limit */
	caller->krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
	caller->krnl->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

	*alloc_addr = old_sbrk;

	caller->krnl->mm = os_mm;

	pthread_mutex_unlock(&mmvm_lock);
	return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
	pthread_mutex_lock(&mmvm_lock);

	if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
	{
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	struct mm_struct * os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	/* TODO: Manage the collect freed region to freerg_list */
	struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

	if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
	{
		caller->krnl->mm = os_mm;

		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}
	struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
	freerg_node->rg_start = rgnode->rg_start;
	freerg_node->rg_end = rgnode->rg_end;
	freerg_node->rg_next = NULL;

	rgnode->rg_start = rgnode->rg_end = 0;
	rgnode->rg_next = NULL;

	/*enlist the obsoleted memory region */
	enlist_vm_freerg_list(caller->krnl->mm, freerg_node);

	caller->krnl->mm = os_mm;

	pthread_mutex_unlock(&mmvm_lock);
	return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
	addr_t addr;
	int val = __alloc(proc, 0, reg_index, size, &addr);
	if (val == -1)
	{
		return -1;
	}
#ifdef IODUMP
	/* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
	print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

	/* By default using vmaid = 0 */
	return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
	int val = __free(proc, 0, reg_index);
	if (val == -1)
	{
		return -1;
	}
	printf("%s:%d\n", __func__, __LINE__);
#ifdef IODUMP
	/* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
	print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
	return 0; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
	uint32_t pte = pte_get_entry(caller, pgn);

	if (!PAGING_PAGE_PRESENT(pte))
	{ /* Page is not online, make it actively living */
		addr_t vicpgn, swpfpn;
		addr_t vicfpn;
		addr_t tgtswpfpn;
		struct sc_regs regs;

		/* Find victim page from the kernel FIFO queue */
		if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
		{
			return -1;
		}

		/* Get a free frame in swap space */
		if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
		{
			return -1;
		}

		/* Get the victim page's current RAM frame number */
		vicfpn = PAGING_FPN(pte_get_entry(caller, vicpgn));

		/* Get the target page's current swap frame number */
		tgtswpfpn = PAGING_SWP(pte);

		/*
		 * Step 1: Swap victim page OUT of RAM into swap space.
		 * SWP(vicfpn <--> swpfpn): moves victim's RAM frame to swap.
		 * a2 = RAM frame, a3 = swap frame destination.
		 */
		regs.a1 = SYSMEM_SWP_OP;
		regs.a2 = vicfpn;   /* source: RAM frame holding victim */
		regs.a3 = swpfpn;   /* dest: free swap frame */
		_syscall(caller->krnl, caller->pid, 17, &regs);

		/* Update victim's PTE to mark it as swapped out to swpfpn */
		pte_set_swap(caller, vicpgn, caller->krnl->active_mswp_id, swpfpn);

		/*
		 * Step 2: Load target page INTO RAM using the now-free vicfpn.
		 * SWP(vicfpn <--> tgtswpfpn): moves target's swap frame to RAM.
		 * a2 = RAM frame (destination), a3 = swap frame (source).
		 */
		regs.a1 = SYSMEM_SWP_OP;
		regs.a2 = vicfpn;       /* dest: freed RAM frame */
		regs.a3 = tgtswpfpn;    /* source: target's swap frame */
		_syscall(caller->krnl, caller->pid, 17, &regs);

		/* Update target's PTE to mark it as present at vicfpn in RAM */
		pte_set_fpn(caller, pgn, vicfpn);

		/* Return the target's old swap frame to the free list */
		MEMPHY_put_freefp(caller->krnl->active_mswp, tgtswpfpn);

		/* Enlist the newly loaded page into the FIFO tracking queue */
		enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
	}

	*fpn = PAGING_FPN(pte_get_entry(caller, pgn));

	return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
	int pgn = PAGING_PGN(addr);
	int off = PAGING_OFFST(addr);
	int fpn;

	/* Bring the page into RAM (swap if necessary) */
	if (pg_getpage(mm, pgn, &fpn, caller) != 0)
		return -1; /* invalid page access */

	/* Compute the physical address: frame base + page offset */
	int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

	/*
	 * Issue SYSMEM_IO_READ via sys_memmap (SYSCALL 17).
	 * a1 = operation, a2 = physical address, result returned in a3.
	 */
	struct sc_regs regs;
	regs.a1 = SYSMEM_IO_READ;
	regs.a2 = phyaddr;
	regs.a3 = 0;
	_syscall(caller->krnl, caller->pid, 17, &regs);

	/* Retrieve the byte read from physical memory */
	*data = (BYTE)regs.a3;

	return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
	int pgn = PAGING_PGN(addr);
	int off = PAGING_OFFST(addr);
	int fpn;

	/* Get the page to MEMRAM, swap from MEMSWAP if needed */
	if (pg_getpage(mm, pgn, &fpn, caller) != 0)
		return -1; /* invalid page access */

	/* Compute the physical address: frame base + page offset */
	int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;

	/*
	 * Issue SYSMEM_IO_WRITE via sys_memmap (SYSCALL 17).
	 * a1 = operation, a2 = physical address, a3 = byte value to write.
	 */
	struct sc_regs regs;
	regs.a1 = SYSMEM_IO_WRITE;
	regs.a2 = phyaddr;
	regs.a3 = (addr_t)value;
	_syscall(caller->krnl, caller->pid, 17, &regs);

	return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
	pthread_mutex_lock(&mmvm_lock);
	struct mm_struct * os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);
	struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

	/* Validate: region and VMA must exist */
	if (currg == NULL || cur_vma == NULL)
	{
		caller->krnl->mm = os_mm;
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	/* Validate: region must be non-empty (has been allocated) */
	if (currg->rg_start == 0 && currg->rg_end == 0)
	{
		caller->krnl->mm = os_mm;
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	/* Validate: offset must be within the allocated region bounds */
	if (currg->rg_start + offset >= currg->rg_end)
	{
		caller->krnl->mm = os_mm;
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

	caller->krnl->mm = os_mm;
	pthread_mutex_unlock(&mmvm_lock);

	return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
	struct pcb_t *proc, // Process executing the instruction
	uint32_t source,	// Index of source register
	addr_t offset,		// Source address = [source] + [offset]
	uint32_t *destination)
{
	BYTE data;
	printf("%s:%d\n", __func__, __LINE__);
	int val = __read(proc, 0, source, offset, &data);

	*destination = data;
#ifdef IODUMP
	/* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
	print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

	return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
	pthread_mutex_lock(&mmvm_lock);
	struct mm_struct * os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

	struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

	if (currg == NULL || cur_vma == NULL) /* Invalid memory identify */
	{
		caller->krnl->mm = os_mm;
		pthread_mutex_unlock(&mmvm_lock);
		return -1;
	}

	pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

	caller->krnl->mm = os_mm;

	pthread_mutex_unlock(&mmvm_lock);
	return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
	struct pcb_t *proc,	  // Process executing the instruction
	BYTE data,			  // Data to be wrttien into memory
	uint32_t destination, // Index of destination register
	addr_t offset)
{
	int val = __write(proc, 0, destination, offset, data);
	if (val == -1)
	{
		return -1;
	}
#ifdef IODUMP
	/* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
	print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

	return val;
}

/*libkmem_malloc - alloc region memory in kmem
 *@caller: caller
 *@size: memory size
 *@reg_index: memory region index in the kernel symbol table
 */
int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
	addr_t addr;

	/* Forward to internal kernel allocator */
	int val = (int)__kmalloc(caller, 0, reg_index, size, &addr);
	if (val != 0)
		return -1;

	return 0;
}

/*__kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 *
 * Unlike __alloc, this operates on the KERNEL's mm directly without
 * swapping – krnl->mm is the authoritative kernel address space.
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
	struct krnl_t *krnl = caller->krnl;

	if (vmaid < 0)
		vmaid = 0; /* default to kernel VMA 0 */

	struct vm_area_struct *cur_vma = get_vma_by_num(krnl->mm, vmaid);
	if (cur_vma == NULL)
		return -1;

	struct vm_rg_struct rgnode;

	/*
	 * First attempt: satisfy from the kernel free-region list.
	 * get_free_vmrg_area uses caller->krnl->mm which, in kernel
	 * context (no mm swap has been performed), IS the kernel mm.
	 */
	if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
	{
		if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
		{
			krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
			krnl->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
		}
		*alloc_addr = rgnode.rg_start;
		return 0;
	}

	/*
	 * No free region available – expand the kernel VMA via sys_memmap.
	 * Record sbrk before the call so we know the new region's start.
	 */
	addr_t old_sbrk = cur_vma->sbrk;

	struct sc_regs regs;
	regs.a1 = SYSMEM_INC_OP;
	regs.a2 = vmaid;
#ifdef MM64
	regs.a3 = size;
#else
	regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
	_syscall(krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */

	/* Record the allocation in the kernel's symbol table */
	if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
	{
		krnl->mm->symrgtbl[rgid].rg_start = old_sbrk;
		krnl->mm->symrgtbl[rgid].rg_end   = old_sbrk + size;
	}

	*alloc_addr = old_sbrk;

	/* Update krnl_pgd for OS kernel-level page directory management */
	if (krnl->krnl_pgd != NULL)
	{
		int start_pgn = old_sbrk / PAGING64_PAGESZ;
		int end_pgn   = (old_sbrk + size - 1) / PAGING64_PAGESZ;
		int i;
		for (i = start_pgn; i <= end_pgn; i++)
			krnl->krnl_pgd[i] = krnl->mm->pgd[i];
	}

	return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: size of each cache slot (object size)
 *@align: alignment of each slot
 *@cache_pool_id: cache pool ID (index into kcpooltbl)
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
	struct krnl_t *krnl = caller->krnl;

	if (krnl->mm == NULL || krnl->mm->kcpooltbl == NULL)
		return -1;

	/*
	 * Allocate backing storage for the cache pool from kernel memory.
	 * We pre-allocate a single page worth of storage for the pool; each
	 * subsequent cache_alloc will carve aligned slots out of this space.
	 */
	addr_t storage_addr = 0;
	uint32_t pool_storage_size = PAGING_PAGE_ALIGNSZ(size); /* one page minimum */

	if (__kmalloc(caller, 0, -1, pool_storage_size, &storage_addr) != 0)
		return -1;

	/* Fill in the pool descriptor */
	krnl->mm->kcpooltbl[cache_pool_id].size    = (int)size;
	krnl->mm->kcpooltbl[cache_pool_id].align   = (int)align;
	krnl->mm->kcpooltbl[cache_pool_id].storage = storage_addr;

	return 0;
}

/*libkmem_cache_alloc - allocate a single cache slot from a pool
 *@proc: caller process
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index for the symbol table
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
	addr_t addr;
	int val = (int)__kmem_cache_alloc(proc, -1, (int)reg_index, (int)cache_pool_id, &addr);
	if (val != 0)
		return -1;

	return 0;
}

/*__kmem_cache_alloc - alloc one slot from a kernel cache pool
 *@caller: caller
 *@vmaid: ID vm area (unused at this level; slot is within pre-allocated pool)
 *@rgid: memory region ID – records the slot in the kernel symbol table
 *@cache_pool_id: cached pool ID
 *@alloc_addr: output – starting address of the allocated slot
 *
 * Uses a bump-pointer strategy: each call advances the pool's storage
 * pointer by one (aligned) slot.  The pool must have been created with
 * libkmem_cache_pool_create beforehand.
 */
addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
	struct krnl_t *krnl = caller->krnl;

	if (krnl->mm == NULL || krnl->mm->kcpooltbl == NULL)
		return (addr_t)-1;

	struct kcache_pool_struct *pool = &krnl->mm->kcpooltbl[cache_pool_id];

	if (pool->size <= 0)
		return (addr_t)-1;

	/* Compute the aligned start of the next free slot */
	addr_t slot_addr = pool->storage;
	if (pool->align > 1)
	{
		/* Round up to the next alignment boundary */
		addr_t mask = (addr_t)(pool->align - 1);
		slot_addr   = (slot_addr + mask) & ~mask;
	}

	/* Register the slot in the kernel symbol table */
	if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
	{
		krnl->mm->symrgtbl[rgid].rg_start = slot_addr;
		krnl->mm->symrgtbl[rgid].rg_end   = slot_addr + (addr_t)pool->size;
	}

	*alloc_addr = slot_addr;

	/* Advance the pool's free pointer past this slot */
	pool->storage = slot_addr + (addr_t)pool->size;

	return 0;
}

/*libkmem_copy_from_user - copy @size bytes from a user memory region to
 *                         a kernel memory region
 *@caller:      calling process
 *@source:      user-space register index (symbol-table entry)
 *@destination: kernel-space register index (symbol-table entry)
 *@offset:      byte offset within the user source region to begin reading
 *@size:        number of bytes to copy
 */
int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
	uint32_t i;

	for (i = 0; i < size; i++)
	{
		BYTE data = 0;

		/* Read one byte from the user address space */
		if (__read_user_mem(caller, 0, (int)source, (addr_t)(offset + i), &data) != 0)
			return -1;

		/* Write that byte into the kernel address space */
		if (__write_kernel_mem(caller, 0, (int)destination, (addr_t)i, data) != 0)
			return -1;
	}

	return 0;
}

/*libkmem_copy_to_user - copy @size bytes from a kernel memory region to
 *                       a user memory region
 *@caller:      calling process
 *@source:      kernel-space register index (symbol-table entry)
 *@destination: user-space register index (symbol-table entry)
 *@offset:      byte offset within the user destination region to begin writing
 *@size:        number of bytes to copy
 */
int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
	uint32_t i;

	for (i = 0; i < size; i++)
	{
		BYTE data = 0;

		/* Read one byte from the kernel address space */
		if (__read_kernel_mem(caller, 0, (int)source, (addr_t)i, &data) != 0)
			return -1;

		/* Write that byte into the user address space */
		if (__write_user_mem(caller, 0, (int)destination, (addr_t)(offset + i), data) != 0)
			return -1;
	}

	return 1;
}

/*__read_kernel_mem - read one byte from a kernel memory region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: kernel symbol-table region index
 *@offset: byte offset within the region
 *@data: output byte
 *
 * Operates on krnl->mm directly – no mm swap needed for kernel access.
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

	/* Validate the kernel region */
	if (currg == NULL)
		return -1;
	if (currg->rg_start == 0 && currg->rg_end == 0)
		return -1;
	if (currg->rg_start + offset >= currg->rg_end)
		return -1;

	/* Read from kernel physical memory via the page-value path.
	 * krnl->mm is already the kernel mm, so no swap is required. */
	if (pg_getval(caller->krnl->mm, (int)(currg->rg_start + offset), data, caller) != 0)
		return -1;

	return 0;
}

/*__write_kernel_mem - write one byte to a kernel memory region
 *@caller: caller
 *@vmaid: ID vm area
 *@rgid: kernel symbol-table region index
 *@offset: byte offset within the region
 *@value: byte to write
 *
 * Operates on krnl->mm directly – no mm swap needed for kernel access.
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

	/* Validate the kernel region */
	if (currg == NULL)
		return -1;
	if (currg->rg_start == 0 && currg->rg_end == 0)
		return -1;
	if (currg->rg_start + offset >= currg->rg_end)
		return -1;

	/* Write to kernel physical memory via the page-value path */
	if (pg_setval(caller->krnl->mm, (int)(currg->rg_start + offset), value, caller) != 0)
		return -1;

	return 0;
}

/*__read_user_mem - read one byte from a user memory region
 *@caller: caller
 *@vmaid: ID vm area
 *@rgid: user symbol-table region index
 *@offset: byte offset within the region
 *@data: output byte
 *
 * Temporarily swaps krnl->mm to the process mm (same pattern as __alloc)
 * so that the page-table walk operates on the user address space.
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
	/* Swap to user process mm */
	struct mm_struct *os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

	/* Validate the user region */
	if (currg == NULL)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}
	if (currg->rg_start == 0 && currg->rg_end == 0)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}
	if (currg->rg_start + offset >= currg->rg_end)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}

	/* Perform the read through the user page table */
	int ret = pg_getval(caller->krnl->mm, (int)(currg->rg_start + offset), data, caller);

	/* Restore kernel mm */
	caller->krnl->mm = os_mm;

	return ret;
}

/*__write_user_mem - write one byte to a user memory region
 *@caller: caller
 *@vmaid: ID vm area
 *@rgid: user symbol-table region index
 *@offset: byte offset within the region
 *@value: byte to write
 *
 * Temporarily swaps krnl->mm to the process mm so that the page-table
 * walk operates on the user address space.
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
	/* Swap to user process mm */
	struct mm_struct *os_mm = caller->krnl->mm;
	caller->krnl->mm = caller->mm;

	struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

	/* Validate the user region */
	if (currg == NULL)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}
	if (currg->rg_start == 0 && currg->rg_end == 0)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}
	if (currg->rg_start + offset >= currg->rg_end)
	{
		caller->krnl->mm = os_mm;
		return -1;
	}

	/* Perform the write through the user page table */
	int ret = pg_setval(caller->krnl->mm, (int)(currg->rg_start + offset), value, caller);

	/* Restore kernel mm */
	caller->krnl->mm = os_mm;

	return ret;
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
	pthread_mutex_lock(&mmvm_lock);
	int pagenum, fpn;
	uint32_t pte;

	for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
	{
		pte = caller->krnl->mm->pgd[pagenum];

		if (PAGING_PAGE_PRESENT(pte))
		{
			fpn = PAGING_FPN(pte);
			MEMPHY_put_freefp(caller->krnl->mram, fpn);
		}
		else
		{
			fpn = PAGING_SWP(pte);
			MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
		}
	}

	pthread_mutex_unlock(&mmvm_lock);
	return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
	struct pgn_t *pg = mm->fifo_pgn;

	/* TODO: Implement the theorical mechanism to find the victim page */
	if (!pg)
	{
		return -1;
	}
	struct pgn_t *prev = NULL;
	while (pg->pg_next)
	{
		prev = pg;
		pg = pg->pg_next;
	}
	*retpgn = pg->pgn;

	/* Detach the victim node from the tail of the FIFO list */
	if (prev != NULL)
		prev->pg_next = NULL;
	else
		mm->fifo_pgn = NULL; /* list had exactly one entry */

	free(pg);

	return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
	struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

	struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

	if (rgit == NULL)
		return -1;

	/* Probe unintialized newrg */
	newrg->rg_start = newrg->rg_end = -1;

	/* Traverse on list of free vm region to find a fit space */
	while (rgit != NULL)
	{
		if (rgit->rg_start + size <= rgit->rg_end)
		{ /* Current region has enough space */
			newrg->rg_start = rgit->rg_start;
			newrg->rg_end = rgit->rg_start + size;

			/* Update left space in chosen region */
			if (rgit->rg_start + size < rgit->rg_end)
			{
				rgit->rg_start = rgit->rg_start + size;
			}
			else
			{ /*Use up all space, remove current node */
				/*Clone next rg node */
				struct vm_rg_struct *nextrg = rgit->rg_next;

				/*Cloning */
				if (nextrg != NULL)
				{
					rgit->rg_start = nextrg->rg_start;
					rgit->rg_end = nextrg->rg_end;

					rgit->rg_next = nextrg->rg_next;

					free(nextrg);
				}
				else
				{								   /*End of free list */
					rgit->rg_start = rgit->rg_end; // dummy, size 0 region
					rgit->rg_next = NULL;
				}
			}
			break;
		}
		else
		{
			rgit = rgit->rg_next; // Traverse next rg
		}
	}

	if (newrg->rg_start == -1) // new region not found
		return -1;

	return 0;
}

// #endif