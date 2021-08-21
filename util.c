#include "util.h"
#include <stddef.h>

/* Initialize the process queue before using */
void init_queue(queue_t *queue)
{
    queue->in = 0;
    queue->out = 0;
    queue->cap = QUEUE_SIZE;
}

/* check priority of process and place process into the queue.
 * Process with highest priority will be placed in front of the queue
 */
static void check(struct process_t *proc, queue_t *queue)
{
    int i;
    for (i = queue->out; i <= queue->in; i++) {
        if (proc->prio <= queue->pool[i].prio || queue->pool[i].pid == 0) {
            for (int j = queue->in + 1; j > i; j--)
                queue->pool[j] = queue->pool[j - 1];
            queue->pool[i] = *proc;
            return;
        }
    }
    queue->pool[i] = *proc;
}

/* Add new process into the queue.
 * @param pointer to the process
 * @param pointer to the queue
 */
void append(struct process_t *proc, queue_t *queue)
{
    check(proc, queue);
    queue->in = (queue->in + 1) % queue->cap;
}

/* Take the highest priority process out of the queue
 * @param pointer to the queue
 * @return pointer to the process
 */
struct process_t *take(queue_t *queue)
{
    if (queue->out == queue->in)
        return NULL;

    struct process_t *proc = &(queue->pool[queue->out]);
    queue->out = (queue->out + 1) % queue->cap;
    return proc;
}
