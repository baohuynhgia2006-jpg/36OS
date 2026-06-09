#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

static int effective_priority(struct pcb_t *proc)
{
#ifdef MLQ_SCHED
	return (int)proc->prio;
#else
	return (int)proc->priority;
#endif
}

int empty(struct queue_t *q)
{
	if (q == NULL)
		return 1;
	return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
	/* TODO: put a new process to queue [q] */
	if (q == NULL || proc == NULL)
		return;
	if (q->size == MAX_QUEUE_SIZE)
		return; /* queue is full */

	q->proc[q->size] = proc;
	q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
	/* TODO: return a pcb whose priority is the highest
	 * in the queue [q] and remember to remove it from q
	 *
	 * "Highest priority" means the smallest numerical value
	 * in the [priority] field of pcb_t.
	 * For MLQ per-priority queues every element has the same
	 * priority, so this degenerates to FIFO (index 0 wins).
	 */
	if (q == NULL || q->size == 0)
		return NULL;

	/* Find the index of the process with the highest priority */
	int best = 0;
	int i;
	for (i = 1; i < q->size; i++)
	{
		if (effective_priority(q->proc[i]) < effective_priority(q->proc[best]))
			best = i;
	}

	struct pcb_t *proc = q->proc[best];

	/* Compact the array: shift everything left from [best+1] */
	for (i = best; i < q->size - 1; i++)
		q->proc[i] = q->proc[i + 1];

	q->size--;
	q->proc[q->size] = NULL;
	return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
	/* TODO: remove a specific item from queue */
	if (q == NULL || proc == NULL)
		return NULL;

	int i;
	for (i = 0; i < q->size; i++)
	{
		if (q->proc[i] == proc)
		{
			/* Found — shift remaining elements left */
			int j;
			for (j = i; j < q->size - 1; j++)
				q->proc[j] = q->proc[j + 1];
			q->size--;
			q->proc[q->size] = NULL;
			return proc;
		}
	}
	return NULL;
}