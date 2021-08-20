#include "util.h"

// Function to check priority and place element
static void check(struct process_t *process, struct queue_t *queue);

/* Initialize the process queue before using */
void init_queue(struct queue_t *queue)
{
    queue->in = 0;
    queue->out = 0;
    queue->k = QUEUE_SIZE;
}

/* Add new process into the queue.
 * @param pointer to the process
 * @param pointer to the queue
 */
void append(struct process_t *process, struct queue_t *queue)
{
    check(process, queue);
    queue->in = (queue->in + 1) % queue->k;
}

/* Take the highest priority process out of the queue
 * @param pointer to the queue
 * @return pointer to the process
 */
struct process_t *take(struct queue_t *queue)
{
    struct process_t *w;
    /* if queue is empty then return NULL */
    if (queue->out == queue->in)
        return NULL;
    /* else return pointer to a process */
    w = &(queue->pool[queue->out]);
    queue->out = (queue->out + 1) % queue->k;
    return w;
}

/* check priority of process and place process into the queue.
 * Process with highest priority will be placed in front of the queue
 * @param process
 */
static void check(struct process_t *process, struct queue_t *queue)
{
    int i;
    for (i = queue->out; i <= queue->in; i++) {
        if (process->prio <= queue->pool[i].prio || queue->pool[i].pid == 0) {
            for (int j = queue->in + 1; j > i; j--)
                queue->pool[j] = queue->pool[j - 1];
            queue->pool[i] = *process;
            return;
        }
    }
    queue->pool[i] = *process;
}
