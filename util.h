#pragma once

#include "sched.h"

#define QUEUE_SIZE 100

/* Define a queue can hold up to 100 processes */
typedef struct {
    int in, out;
    int cap;
    struct process_t pool[QUEUE_SIZE];
} queue_t;

void init_queue(queue_t *queue);

/* add a new process into the queue */
void append(struct process_t *process, queue_t *queue);

/* take the highest priority process out of the queue */
struct process_t *take();
