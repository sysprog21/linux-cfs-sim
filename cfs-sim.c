/* CFS simulator includes:
 *   - A producer thread generates 20 processes initially
 *   - 4 consumer threads (4 CPUs) execute processes
 *   - A balancer thread balances the process queues among 4 consumers
 */

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

/* Configurations */
#define PROCESS_FILE "processes.txt"
#define N_CPU 4 /* number of CPUs or consumers */
#define N_RQ 3  /* number of runqueue (RQ)  */

/* a process wait for I/O operation (or event), in ms */
#define WAIT_TIME 1200

/* the artificial sleep time to make output readable, in ms */
#define SLEEP_TIME 1000

/* maximum sleep average time */
#define MAX_SLEEP_AVG 10

/* interval between each load balancer, in second */
#define BALANCER_INTERVAL 2

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* Create a global table of run queues where:
 *   - Row number is CPU number
 *   - Column number is RQ numer
 *
 * Tables:
 * CPU 0: RQ0 RQ1 RQ2
 * CPU 1: RQ0 RQ1 RQ2
 * CPU 2: RQ0 RQ1 RQ2
 * CPU 3: RQ0 RQ1 RQ2
 *
 * E.g., RQ[0,1] -> RQ1 of CPU0
 */
static queue_t RQ[N_CPU][N_RQ];
static pthread_mutex_t qlock[N_CPU][N_RQ]; /* mutex for each queue */

/* string representation of scheduling policy */
static const char *sched_to_str[] = {"SCHED_RR", "SCHED_FIFO", "SCHED_NORMAL"};

static int p_count[N_CPU]; /* # of proc each CPU has */
static int n_process;      /* total # of proc still in the ready queue */
static int b_value; /* # of proc each CPU should have in their ready queue */
static bool running = true; /* indicate if the simulator is still running */

/* Parse process description and convert into a process context.
 * @param string containing the process info
 * @return process_t containing process info extracted from the string
 */
static struct process_t desc2proc(char *line)
{
    struct process_t proc;
    char delimiter[] = " ,";
    char *token = strtok(line, delimiter); /* extract the SCHED info */

    if (!strcmp(token, sched_to_str[RR]))
        proc.sched = RR; /* Round-Robin */
    else if (!strcmp(token, sched_to_str[FIFO]))
        proc.sched = FIFO; /* FIFO */
    else if (!strcmp(token, sched_to_str[NORMAL]))
        proc.sched = NORMAL; /* normal */
    /* FIXME: deal with malformed inputs */

    token = strtok(NULL, delimiter); /* extract the static_prio */
    proc.static_prio = atoi(token);
    token = strtok(NULL, delimiter); /* extract the execution time */
    proc.exec_time = atoi(token);
    return proc;
}

/* Print out to the console the table containing all processes */
static void process_status(void)
{
    queue_t RQ_snapshot[N_CPU][N_RQ];

    /* take a snapshot of the queues */
    memcpy(&RQ_snapshot, &RQ, sizeof(RQ));
    printf("%-3s | %-12s | %-3s | %-8s | %-11s | %-4s | %-9s \n", "PID",
           "SCHED_SCHEME", "CPU", "last_CPU", "static_prio", "prio",
           "exec_time");
    for (int i = 0; i < N_CPU; i++)
        for (int j = 0; j < N_RQ; j++) {
            queue_t queue = RQ_snapshot[i][j];
            for (int k = queue.out; k < queue.in; k++) {
                struct process_t proc = queue.pool[k];
                printf("%-3d | %-12s | %-3d | %-8d | %-11d | %-4d | %-9d \n",
                       proc.pid, sched_to_str[proc.sched], i, proc.last_cpu,
                       proc.static_prio, proc.prio, proc.exec_time);
            }
        }
}

/* Producer implementation.
 * The thread generates processes distributed into the queue of each comsumers.
 * Initially, read processes infos from a file name defined in PROCESS_FILE.
 * Generates 20 processes: 12 SCHED_NORMAL, 4 SCHED_FIFO, 4 SCHED_RR
 * Each run queue will has 5 process intitally
 */
void *producer(void *arg)
{
    char line[100]; /* one line of the text file */
    int cpu;

    printf("[producer]: Producer has been created.\n");
    printf("[producer]: Loading processes. Please wait a few secs....\n");
    FILE *file = fopen(PROCESS_FILE, "r");
    if (!file) {
        fprintf(stderr, "[Producer]: ERROR opening file.\n");
        exit(EXIT_FAILURE);
    }

    /* Read the number of processes in the file */
    fgets(line, 100, file);
    n_process = atoi(line);
    if (n_process % N_CPU != 0) {
        fprintf(stderr,
                "[Producer]: Initial number of processes is invalid.\n");
        exit(EXIT_FAILURE);
    } else {
        /* b_value is the number of processes each CPU should has, so that
         * all CPUs has the same number of processes.
         */
        b_value = n_process / N_CPU;
    }

    /* Initially, each CPU has 0 process in the queue */
    for (int i = 0; i < N_CPU; i++)
        p_count[i] = 0;

    /* Initialize the queue for each CPU */
    for (int i = 0; i < N_CPU; i++) {
        for (int j = 0; j < N_RQ; j++) {
            init_queue(&RQ[i][j]);
            if (pthread_mutex_init(&qlock[i][j], NULL) != 0) {
                fprintf(stderr, "[Producer]: Mutex initialization failed.\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* Read and distribute proccess into ready queue for each consumers */
    int count = 1;
    while (1) {
        if (!fgets(line, 100, file)) /* reach EOF */
            break;

        struct process_t proc =
            desc2proc(line); /* convert string into process */
        proc.pid = count++;  /* assign pid to process */
        proc.prio = proc.static_prio;
        proc.time_slice = 0;
        proc.accu_time_slice = 0;
        proc.sleep_avg = 0;

        /* find an available CPU to assign the process into */
        do {
            srand(time(NULL));  /* seed the random generator */
            cpu = (rand() % 4); /* generate a random CPU number 0 - 3 */
        } while (p_count[cpu] >= b_value);

        /* assign the process into the CPU queue */
        proc.last_cpu = cpu;

        /* If process is SCHED_NORMAL, then store in RQ1. */
        if (proc.sched == NORMAL) {
            pthread_mutex_lock(&qlock[cpu][1]);
            append(&proc, &RQ[cpu][1]);
            pthread_mutex_unlock(&qlock[cpu][1]);
        } else { /* store process in RQ0 */
            pthread_mutex_lock(&qlock[cpu][0]);
            append(&proc, &RQ[cpu][0]);
            pthread_mutex_unlock(&qlock[cpu][0]);
        }

        p_count[cpu]++; /* increase the number of processes for that CPU */
    }
    process_status();
    sleep(2);
    pthread_exit(NULL);
}

/* called by the CPU to execute a SCHED_FIFO process */
static void execute_FIFO(int cpu, struct process_t *proc)
{
    /* a FIFO process run to completion */
    printf("[CPU %d]: executing PID %d, %s.\n", cpu, proc->pid,
           sched_to_str[proc->sched]);
    usleep(proc->exec_time * 1000);
    printf("[CPU %d]: PID %d is finished.\n", cpu, proc->pid);
    process_status();

    /* update counters */
    p_count[cpu]--; /* decrement number of processes this CPU has */
    n_process--;    /* decrement total number of processes still in the queue */
}

/* called by the CPU to execute a SCHED_RR process */
static void execute_RR(int cpu, struct process_t *proc)
{
    printf("[CPU %d]: executing PID %d, %s.\n", cpu, proc->pid,
           sched_to_str[proc->sched]);

    /* Calculate timeslice for the process */
    if (proc->prio < 120)
        proc->time_slice = (140 - proc->prio) * 20;
    else
        proc->time_slice = (140 - proc->prio) * 5;

    /* Calculate the remaining execution time of the process */
    int remaining_exec_time = proc->exec_time - proc->accu_time_slice;

    /* Accumulate time_slice */
    proc->accu_time_slice += proc->time_slice;

    /* execute process */
    if (remaining_exec_time > proc->time_slice) {
        usleep((proc->time_slice + SLEEP_TIME) * 1000);
        printf(
            "[CPU %d]: PID %d is preempted. Remaining execution time = "
            "%d.\n",
            cpu, proc->pid, proc->exec_time - proc->accu_time_slice);

        /* Put back the process into the queue RQ0 */
        pthread_mutex_lock(&qlock[cpu][0]);
        append(proc, &RQ[cpu][0]);
        pthread_mutex_unlock(&qlock[cpu][0]);
    } else {
        usleep((remaining_exec_time + SLEEP_TIME) * 1000);
        printf("[CPU %d]: PID %d is finished.\n", cpu, proc->pid);
        process_status();

        /* update counters */
        p_count[cpu]--; /* reduce number of processes this CPU has */
        n_process--; /* reduce total number of processes still in the queue */
    }
}

/* called by the CPU to execute a SCHED_NORMAL process */
static void execute_NORMAL(int cpu, struct process_t *proc)
{
    struct timeval t1, t2;

    /* Calculate the time ticks */
    t1 = proc->last_run;
    gettimeofday(&t2, NULL);
    int deltaT =
        (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_usec - t1.tv_usec) / 1000;
    int ticks = deltaT / 200;
    ticks = (ticks < MAX_SLEEP_AVG) ? ticks : MAX_SLEEP_AVG;

    /* add tick to the sleep_avg */
    proc->sleep_avg += ticks;
    proc->sleep_avg =
        (proc->sleep_avg < MAX_SLEEP_AVG) ? proc->sleep_avg : MAX_SLEEP_AVG;

    /* Calculate the dynamic priority */
    proc->prio = MAX(100, MIN(proc->prio - proc->sleep_avg + 5, 139));

    /* Calculate timeslice for the process */
    if (proc->prio < 120)
        proc->time_slice = (140 - proc->prio) * 20;
    else
        proc->time_slice = (140 - proc->prio) * 5;

    /* Calculate the remaining execution time of the process */
    int remaining_exec_time = proc->exec_time - proc->accu_time_slice;

    /* Accumulate time_slice */
    proc->accu_time_slice += proc->time_slice;

    /* service_time is between 10 ms and time_slice */
    srand(time(NULL));  // seed the random generator
    int service_time = 10 + (rand() % (proc->time_slice - 9));

    /* execute process */
    printf("[CPU %d]: executing PID %d, %s, service_time = %d.\n", cpu,
           proc->pid, sched_to_str[proc->sched], service_time);
    if (remaining_exec_time > service_time) {
        /* Record the time before executing the process */
        gettimeofday(&t1, NULL);

        /* execute the process */
        usleep((service_time + SLEEP_TIME) * 1000);

        /* Record the time after executing the process */
        gettimeofday(&t2, NULL);

        /* Record the last_run */
        proc->last_run = t2;

        /* Calculate the execution time */
        deltaT =
            (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_usec - t1.tv_usec) / 1000;
        deltaT = deltaT - SLEEP_TIME; /* substract artificial SLEEP_TIME */
        ticks = deltaT / 100;

        /* Substract the ticks from sleep_avg */
        proc->sleep_avg = proc->sleep_avg - ticks;

        /* if the process is waiting for I/O event */
        if (service_time < proc->time_slice) {
            printf("[CPU %d]: PID %d is blocked waiting for an event.\n", cpu,
                   proc->pid);
            usleep(WAIT_TIME * 1000); /* simulate the blocked time */
        }
        printf(
            "[CPU %d]: PID %d is preempted. Remaining execution time = "
            "%d.\n",
            cpu, proc->pid, proc->exec_time - proc->accu_time_slice);

        /* Put back the process into the queue */
        if (proc->prio < 130) { /* if prio < 130, process go to queue 1 */
            pthread_mutex_lock(&qlock[cpu][1]);
            append(proc, &RQ[cpu][1]);
            pthread_mutex_unlock(&qlock[cpu][1]);
        } else { /* if prio >= 130, process go to queue 2 */
            pthread_mutex_lock(&qlock[cpu][2]);
            append(proc, &RQ[cpu][2]);
            pthread_mutex_unlock(&qlock[cpu][2]);
        }
    } else {
        usleep((remaining_exec_time + SLEEP_TIME) * 1000);
        printf("[CPU %d]: PID %d is finished.\n", cpu, proc->pid);
        process_status();

        /* update counters */
        p_count[cpu]--; /* decrement number of processes this CPU has */
        n_process--;    /* decrement total number of processes still in queue */
    }
}

/* Consumer implementation.
 * When created, the *arg tell the thread its CPU number (From 0 to 3)
 * The thread execute the processes in its own ready queue.
 * The processes has 3 queues: RQ0, RQ1, RQ3.
 */
void *consumer(void *arg)
{
    int cpu = *(int *) arg; /* CPU number assigned from for this consumer */
    struct process_t *proc;

    printf("[CPU %d]: thread has been created.\n", cpu);
    while (running) {
        for (int i = 0; i < N_RQ; i++) { /* go through each queue */
            if (!running)
                break;

            /* take a process out of the queue */
            pthread_mutex_lock(&qlock[cpu][i]);
            proc = take(&RQ[cpu][i]);
            pthread_mutex_unlock(&qlock[cpu][i]);

            /* go to next queue if there is no more process in queue */
            if (!proc)
                break;

            /* print out process info */
            printf("[CPU %d]: PID %d is selected,%s, priority = %d.\n", cpu,
                   proc->pid, sched_to_str[proc->sched], proc->prio);

            /* execute the process based on its scheduling policy */
            switch (proc->sched) {
            case FIFO: /* SCHED_FIFO */
                execute_FIFO(cpu, proc);
                break;
            case RR: /* SCHED_RR */
                execute_RR(cpu, proc);
                break;
            case NORMAL: /* SCHED_NORMAL */
                execute_NORMAL(cpu, proc);
                break;
            default: /* invalid process */
                printf(
                    "[CPU %d]: PROCESS PID %d has invalid scheduling policy.\n",
                    cpu, proc->pid);
            }
        }
    }
    pthread_exit(NULL);
}

/* The thread balance the number of queues distributed for each consumers. */
void *balancer(void *arg)
{
    int current_queue;

    printf("[balancer]: Balancer has been created.\n");
    while (running) {
        if (n_process <= 0) { /* check if there are no more process */
            printf("[Balancer] No more process to execute. Stop.\n");
            running = false;
            break;
        }

        /* b_value is the number of processes each CPU should have so that
         * processes are distributed equally to all CPUs.
         */
        b_value = n_process / N_CPU;
        for (int i = 0; i < N_CPU; i++) {
            while (p_count[i] > b_value) { /* more than b_value processes */
                /* take out one process from the CPU's queue */
                struct process_t *proc = NULL;

                int j = 0;
                for (j = N_RQ - 1; j >= 0; j--) {
                    pthread_mutex_lock(&qlock[i][j]);
                    proc = take(&RQ[i][j]);
                    pthread_mutex_unlock(&qlock[i][j]);

                    /* update p_count */
                    p_count[i]--;
                    if (proc) {
                        current_queue = j;
                        break;
                    }
                }

                /* if cannot take out any process then break */
                if (!proc)
                    break;

                /* find a CPU to move the process over */
                for (j = 0; j < N_CPU; j++) {
                    if (j != i && p_count[j] < b_value)
                        break;
                }

                /* if cannot find any available cpu, then break */
                if (j == N_CPU || (p_count[j] >= b_value)) {
                    /* put back the process into the queue */
                    pthread_mutex_lock(&qlock[i][current_queue]);
                    append(proc, &RQ[i][current_queue]);
                    pthread_mutex_unlock(&qlock[i][current_queue]);
                    p_count[i]++;
                    break;
                }

                /* found a new cpu for process */
                int new_cpu = j;

                /* update process info */
                proc->last_cpu = i;
                printf(
                    "[Balancer]: PID %d is moved from CPU %d to CPU "
                    "%d.\n",
                    proc->pid, i, j);

                /* add process into new cpu's queue.
                 * if process is SCHED_NORMAL, then store in RQ1.
                 */
                if (proc->sched == NORMAL) {
                    pthread_mutex_lock(&qlock[new_cpu][1]);
                    append(proc, &RQ[new_cpu][1]);
                    pthread_mutex_unlock(&qlock[new_cpu][1]);
                } else { /* store process in RQ0 */
                    pthread_mutex_lock(&qlock[new_cpu][0]);
                    append(proc, &RQ[new_cpu][0]);
                    pthread_mutex_unlock(&qlock[new_cpu][0]);
                }

                /* update p_count */
                p_count[new_cpu]++;
            }
        }
        sleep(BALANCER_INTERVAL);
    }
    pthread_exit(NULL);
}

int main(int argc, char const *argv[])
{
    pthread_t producer_thread, balancer_thread;
    pthread_t consumer_thread[N_CPU];
    pthread_attr_t attr;

    /* trigger producer thread to create 20 processes in the queue */
    int res = pthread_create(&producer_thread, NULL, producer, (void *) NULL);
    if (res != 0) {
        fprintf(stderr, "[main]: producer thread creation failed.\n");
        exit(EXIT_FAILURE);
    }
    res = pthread_join(producer_thread, (void *) NULL);
    if (res != 0) {
        fprintf(stderr, "[main]: producer thread join failed.\n");
        exit(EXIT_FAILURE);
    }

    /* create detached thread attr */
    pthread_attr_init(&attr);
    res = pthread_attr_init(&attr);
    if (res != 0) {
        fprintf(stderr, "[main]: Attribute creation failed.\n");
        exit(EXIT_FAILURE);
    }
    res = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (res != 0) {
        fprintf(stderr, "[main]: Setting detached attribute failed.\n");
        exit(EXIT_FAILURE);
    }

    /* trigger balancer thread */
    res = pthread_create(&balancer_thread, &attr, balancer, (void *) NULL);
    if (res != 0) {
        fprintf(stderr, "[main]: balancer thread creation failed.\n");
        exit(EXIT_FAILURE);
    }

    /* create consumer threads */
    int cpu_count[N_CPU];
    for (int i = 0; i < N_CPU; i++) {
        cpu_count[i] = i;
        res = pthread_create(&consumer_thread[i], &attr, consumer,
                             (void *) &cpu_count[i]);
        if (res != 0) {
            fprintf(stderr, "[main]: consumer thread creation failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    (void) pthread_attr_destroy(&attr);

    while (running)
        /* wait */;

    /* FIXME: reclaim the allocated resources */

    return 0;
}
