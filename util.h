#pragma once

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sched.h"

#define QUEUE_SIZE 100

/* Define a queue can hold up to 100 processes */
struct queue_t {
    int in;
    int out;
    int k;
    struct process_t pool[QUEUE_SIZE];
};

void init_queue(struct queue_t *queue);

/* add a new process into the queue */
void append(struct process_t *process, struct queue_t *queue);

/* take the highest priority process out of the queue */
struct process_t *take();
