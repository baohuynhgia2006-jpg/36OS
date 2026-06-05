
#include <stdio.h>
#include "queue.h"

/*
 * Queue primitive used by the scheduler.
 *
 * This file intentionally does not use mutexes. The scheduler owns the
 * synchronization policy and must hold its lock before touching shared queues.
 */

static int effective_prio(struct pcb_t *proc)
{
        if (proc == NULL)
                return MAX_QUEUE_SIZE;

#ifdef MLQ_SCHED
        return (int)proc->prio;
#else
        return (int)proc->priority;
#endif
}

int empty(struct queue_t *q)
{
        return (q == NULL || q->size <= 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        if (q == NULL || proc == NULL)
                return;

        if (q->size >= MAX_QUEUE_SIZE) {
                fprintf(stderr, "enqueue: queue is full, drop PID %u\n", proc->pid);
                return;
        }

        q->proc[q->size++] = proc;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        int selected;
        struct pcb_t *proc;

        if (empty(q))
                return NULL;

        /*
         * Keep priority-queue behavior for the legacy scheduler.
         * In MLQ mode, each queue normally contains the same priority, so this
         * becomes FIFO because ties keep the earliest inserted process.
         */
        selected = 0;
        for (int i = 1; i < q->size; i++) {
                if (effective_prio(q->proc[i]) < effective_prio(q->proc[selected]))
                        selected = i;
        }

        proc = q->proc[selected];

        for (int i = selected + 1; i < q->size; i++)
                q->proc[i - 1] = q->proc[i];

        q->size--;
        q->proc[q->size] = NULL;

        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        int selected = -1;
        struct pcb_t *removed;

        if (empty(q) || proc == NULL)
                return NULL;

        for (int i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        selected = i;
                        break;
                }
        }

        if (selected < 0)
                return NULL;

        removed = q->proc[selected];

        for (int i = selected + 1; i < q->size; i++)
                q->proc[i - 1] = q->proc[i];

        q->size--;
        q->proc[q->size] = NULL;

        return removed;
}