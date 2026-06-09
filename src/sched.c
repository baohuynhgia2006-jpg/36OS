/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;

static int normalize_prio(uint32_t prio)
{
	return (prio >= MAX_PRIO) ? (MAX_PRIO - 1) : (int)prio;
}

static struct pcb_t *find_proc_in_queue(struct queue_t *q, uint32_t pid)
{
	if (q == NULL)
		return NULL;

	for (int i = 0; i < q->size; i++)
	{
		if (q->proc[i] != NULL && q->proc[i]->pid == pid)
			return q->proc[i];
	}

	return NULL;
}

#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

int queue_empty(void)
{
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if (!empty(&mlq_ready_queue[prio]))
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void)
{
#ifdef MLQ_SCHED
	int i;
	for (i = 0; i < MAX_PRIO; i++)
	{
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i;
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED

/*
 * Stateful MLQ scheduler
 * ─────────────────────
 * State variables (static, survive across calls):
 *   curr_prio     – the priority level currently being served (0 .. MAX_PRIO-1)
 *   curr_slot_cnt – how many slots have already been consumed at curr_prio
 *
 * Policy (from the assignment spec):
 *   slot[p] = MAX_PRIO - p
 *   Each priority-p queue may run slot[p] processes consecutively.
 *   When the slot budget is exhausted (or the queue is found empty),
 *   the scheduler advances to the next priority level (wrapping around).
 *   It tries every level at most once per call before giving up.
 */

struct pcb_t *get_mlq_proc(void)
{
	struct pcb_t *proc = NULL;

	/* Persistent scheduling state */
	static unsigned long curr_prio = 0;
	static int curr_slot_cnt = 0;

	pthread_mutex_lock(&queue_lock);

	/*
	 * Walk at most MAX_PRIO levels looking for a non-empty queue
	 * that still has slot budget left.
	 */

	for (int tried = 0; tried < MAX_PRIO; tried++)
	{
		if (!empty(&mlq_ready_queue[curr_prio]))
		{
			/* This level has a runnable process and budget remaining */
			proc = dequeue(&mlq_ready_queue[curr_prio]);
			curr_slot_cnt++;

			/* Advance to the next level when the budget is exhausted */
			if (curr_slot_cnt >= slot[curr_prio])
			{
				curr_prio = (curr_prio + 1) % MAX_PRIO;
				curr_slot_cnt = 0;
			}
			break; /* found a process — stop searching */
		}

		/*
		 * Queue is empty at this level: skip it and move on.
		 * Reset the slot counter so the level gets a full budget
		 * when it becomes non-empty again.
		 */

		curr_prio = (curr_prio + 1) % MAX_PRIO;
		curr_slot_cnt = 0;
	}

	if (proc != NULL)
		enqueue(&running_list, proc);

	pthread_mutex_unlock(&queue_lock);
	return proc;
}

/*
 * Called when the current time-slice ends and the process goes back
 * into the MLQ ready queue (it is not finished, just preempted).
 *
 * Steps:
 *   1. Remove the process from the running list.
 *   2. Re-enqueue it into its priority level's ready queue.
 */

void put_mlq_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);			 /* remove from running list  */
	proc->prio = (uint32_t)normalize_prio(proc->prio);
	enqueue(&mlq_ready_queue[proc->prio], proc); /* back to its ready queue */
	pthread_mutex_unlock(&queue_lock);
}

/*
 * Called when a brand-new process is admitted to the system.
 * It goes straight into the appropriate MLQ ready queue.
 */

void add_mlq_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	proc->prio = (uint32_t)normalize_prio(proc->prio);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

/* ── Dispatch wrappers (MLQ path) ── */

struct pcb_t *get_proc(void) 
{
	return get_mlq_proc(); 
}

void put_proc(struct pcb_t *proc) 
{
	put_mlq_proc(proc); 
}

void add_proc(struct pcb_t *proc) 
{
	add_mlq_proc(proc); 
}

#else /* ── Non-MLQ (simple priority queue) path ── */

/*
 * Dequeue the highest-priority process from ready_queue,
 * track it in running_list, and return it to the caller.
 */
struct pcb_t *get_proc(void)
{
	struct pcb_t *proc = NULL;

	pthread_mutex_lock(&queue_lock);
	proc = dequeue(&ready_queue);
	if (proc != NULL)
		enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

/*
 * Process has used its time-slice: remove it from running_list and
 * place it in run_queue (legacy "processed" queue).
 */
void put_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

/*
 * Admit a new process: enqueue it into ready_queue.
 */
void add_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}
#endif /* MLQ_SCHED */

struct pcb_t *sched_find_proc_by_pid(struct krnl_t *krnl, uint32_t pid)
{
	struct pcb_t *proc = NULL;

	(void)krnl;
	pthread_mutex_lock(&queue_lock);
	proc = find_proc_in_queue(&running_list, pid);
	if (proc == NULL)
		proc = find_proc_in_queue(&ready_queue, pid);
	if (proc == NULL)
		proc = find_proc_in_queue(&run_queue, pid);
#ifdef MLQ_SCHED
	if (proc == NULL)
	{
		for (int i = 0; i < MAX_PRIO && proc == NULL; i++)
			proc = find_proc_in_queue(&mlq_ready_queue[i], pid);
	}
#endif
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void sched_remove_proc(struct pcb_t *proc)
{
	if (proc == NULL)
		return;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	purgequeue(&ready_queue, proc);
	purgequeue(&run_queue, proc);
#ifdef MLQ_SCHED
	for (int i = 0; i < MAX_PRIO; i++)
		purgequeue(&mlq_ready_queue[i], proc);
#endif
	pthread_mutex_unlock(&queue_lock);
}