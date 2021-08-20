#pragma once

#include <sys/time.h>

enum sched_policy {
    RR,
    FIFO,
    NORMAL,
};

struct process_t {
    int pid;              /* an integer PID created by the producer */
    enum sched_policy sched; /* scheduling policy used for process */
    int static_prio;      /* Static prio, real-time: 0-99, normal: 100-139 */
    int prio;             /* dynamic priority */
    int exec_time;        /* expected execution time */
    int time_slice;       /* time slice for round robin */
    int accu_time_slice;  /* accumulated time slice */
    int sleep_avg;        /* average sleep time */
    int last_cpu;         /* the CPU (thread) that the process last ran */
    struct timeval last_run;  /* record the time the process was executed */
};
