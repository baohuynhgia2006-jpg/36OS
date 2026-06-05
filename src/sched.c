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
#include <stdint.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static struct queue_t running_list;

static pthread_mutex_t queue_lock;

#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

static void init_queue(struct queue_t *q)
{
        if (q == NULL)
                return;

        q->size = 0;
        for (int i = 0; i < MAX_QUEUE_SIZE; i++)
                q->proc[i] = NULL;
}

static void bind_kernel_queues(struct pcb_t *proc)
{
        if (proc == NULL || proc->krnl == NULL)
                return;

        proc->krnl->ready_queue = &ready_queue;
        proc->krnl->running_list = &running_list;

#ifdef MLQ_SCHED
        proc->krnl->mlq_ready_queue = mlq_ready_queue;
#endif
}

static struct pcb_t *find_proc_in_queue(struct queue_t *q, uint32_t pid)
{
        if (q == NULL)
                return NULL;

        for (int i = 0; i < q->size; i++) {
                if (q->proc[i] != NULL && q->proc[i]->pid == pid)
                        return q->proc[i];
        }

        return NULL;
}

#ifdef MLQ_SCHED
static int normalize_prio(uint32_t prio)
{
        if (prio >= MAX_PRIO)
                return MAX_PRIO - 1;

        return (int)prio;
}

static void reset_mlq_slots(void)
{
        for (int i = 0; i < MAX_PRIO; i++)
                slot[i] = MAX_PRIO - i;
}

static int mlq_empty_locked(void)
{
        for (int i = 0; i < MAX_PRIO; i++) {
                if (!empty(&mlq_ready_queue[i]))
                        return 0;
        }

        return 1;
}
#else
static int normal_empty_locked(void)
{
        return empty(&ready_queue) && empty(&run_queue);
}
#endif

int queue_empty(void)
{
        int is_empty;

        pthread_mutex_lock(&queue_lock);

#ifdef MLQ_SCHED
        is_empty = mlq_empty_locked();
#else
        is_empty = normal_empty_locked();
#endif

        pthread_mutex_unlock(&queue_lock);
        return is_empty;
}

/*
 * Helper for later syscall/memory work.
 * It is safe to keep this here even if the current header does not declare it yet.
 */
struct pcb_t *sched_find_proc_by_pid(struct krnl_t *krnl, uint32_t pid)
{
        struct pcb_t *proc = NULL;

        if (krnl == NULL)
                return NULL;

        pthread_mutex_lock(&queue_lock);

        proc = find_proc_in_queue(krnl->running_list, pid);
        if (proc != NULL)
                goto out;

#ifdef MLQ_SCHED
        if (krnl->mlq_ready_queue != NULL) {
                for (int i = 0; i < MAX_PRIO; i++) {
                        proc = find_proc_in_queue(&krnl->mlq_ready_queue[i], pid);
                        if (proc != NULL)
                                goto out;
                }
        }
#else
        proc = find_proc_in_queue(krnl->ready_queue, pid);
        if (proc != NULL)
                goto out;

        proc = find_proc_in_queue(&run_queue, pid);
#endif

out:
        pthread_mutex_unlock(&queue_lock);
        return proc;
}

void sched_remove_proc(struct pcb_t *proc)
{
        if (proc == NULL || proc->krnl == NULL)
                return;

        pthread_mutex_lock(&queue_lock);

        purgequeue(proc->krnl->running_list, proc);

#ifdef MLQ_SCHED
        if (proc->krnl->mlq_ready_queue != NULL) {
                for (int i = 0; i < MAX_PRIO; i++)
                        purgequeue(&proc->krnl->mlq_ready_queue[i], proc);
        }
#else
        purgequeue(proc->krnl->ready_queue, proc);
        purgequeue(&run_queue, proc);
#endif

        pthread_mutex_unlock(&queue_lock);
}

void init_scheduler(void)
{
        init_queue(&ready_queue);
        init_queue(&run_queue);
        init_queue(&running_list);

#ifdef MLQ_SCHED
        for (int i = 0; i < MAX_PRIO; i++)
                init_queue(&mlq_ready_queue[i]);

        reset_mlq_slots();
#endif

        pthread_mutex_init(&queue_lock, NULL);
}

void finish_scheduler(void)
{
        pthread_mutex_destroy(&queue_lock);
}

#ifdef MLQ_SCHED
static struct pcb_t *pick_mlq_proc_locked(void)
{
        for (int pass = 0; pass < 2; pass++) {
                int has_waiting_proc = 0;

                for (int prio = 0; prio < MAX_PRIO; prio++) {
                        if (empty(&mlq_ready_queue[prio]))
                                continue;

                        has_waiting_proc = 1;

                        if (slot[prio] <= 0)
                                continue;

                        slot[prio]--;
                        return dequeue(&mlq_ready_queue[prio]);
                }

                if (!has_waiting_proc)
                        return NULL;

                reset_mlq_slots();
        }

        return NULL;
}

struct pcb_t *get_mlq_proc(void)
{
        struct pcb_t *proc;

        pthread_mutex_lock(&queue_lock);

        proc = pick_mlq_proc_locked();
        if (proc != NULL) {
                bind_kernel_queues(proc);
                enqueue(&running_list, proc);
        }

        pthread_mutex_unlock(&queue_lock);
        return proc;
}

void put_mlq_proc(struct pcb_t *proc)
{
        int prio;

        if (proc == NULL)
                return;

        pthread_mutex_lock(&queue_lock);

        bind_kernel_queues(proc);
        purgequeue(&running_list, proc);

        prio = normalize_prio(proc->prio);
        proc->prio = (uint32_t)prio;
        enqueue(&mlq_ready_queue[prio], proc);

        pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t *proc)
{
        int prio;

        if (proc == NULL)
                return;

        pthread_mutex_lock(&queue_lock);

        bind_kernel_queues(proc);
        prio = normalize_prio(proc->prio);
        proc->prio = (uint32_t)prio;
        enqueue(&mlq_ready_queue[prio], proc);

        pthread_mutex_unlock(&queue_lock);
}

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

#else
struct pcb_t *get_proc(void)
{
        struct pcb_t *proc;

        pthread_mutex_lock(&queue_lock);

        proc = dequeue(&ready_queue);
        if (proc == NULL)
                proc = dequeue(&run_queue);

        if (proc != NULL) {
                bind_kernel_queues(proc);
                enqueue(&running_list, proc);
        }

        pthread_mutex_unlock(&queue_lock);
        return proc;
}

void put_proc(struct pcb_t *proc)
{
        if (proc == NULL)
                return;

        pthread_mutex_lock(&queue_lock);

        bind_kernel_queues(proc);
        purgequeue(&running_list, proc);
        enqueue(&run_queue, proc);

        pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
        if (proc == NULL)
                return;

        pthread_mutex_lock(&queue_lock);

        bind_kernel_queues(proc);
        enqueue(&ready_queue, proc);

        pthread_mutex_unlock(&queue_lock);
}
#endif