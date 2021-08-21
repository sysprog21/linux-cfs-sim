/* CFS simulator includes:
 *   - A producer thread generates 20 processes initially
 *   - 4 consumer threads (4 CPUs) execute processes
 *   - A balancer thread balances the process queues among 4 consumers
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#define PROCESS_FILE "processes.txt"
#define N_CPU 4   /* number of CPUs or consumers */
#define N_QUEUE 3 /* number of RQs */

/* 1200 ms a process wait for I/O operation (or event) */
#define wait_time 1200

/* 1 second artificial sleep time to make output readable */
#define sleep_time 1000

#define mili_to_micro 1000 /* convert milisecond to microsecond */
#define MAX_SLEEP_AVG 10   /* maximum sleep average time */
#define b_freq 2           /* frequency of the balancer */

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
struct queue_t RQ[N_CPU][N_QUEUE];
pthread_mutex_t QM[N_CPU][N_QUEUE]; /* mutex for each queue */

/* string representation of scheduling policy */
const char *sched_to_string[] = {"SCHED_RR", "SCHED_FIFO", "SCHED_NORMAL"};

int p_count[N_CPU]; /* number of processes each CPU has */
int n_process;      /* total number of processes still in the ready queue */
int b_value; /* number of processes each CPU should have in their ready queue */
int running = 1; /* indicate that the emulator is still running */
int cpu_count[N_CPU];

/** producer thread **/
void *producer(void *arg);
struct process_t ltop(char *line); /* convert a string to a process struct */
void process_status(void);         /* print out process status */

/** consumer thread **/
void *consumer(void *arg);
static void execute_FIFO(int CPU, struct process_t *process);
static void execute_RR(int CPU, struct process_t *process);
static void execute_NORMAL(int CPU, struct process_t *process);

/** balancer thread **/
void *balancer(void *arg);

int main(int argc, char const *argv[])
{
    pthread_t producer_thread, balancer_thread;
    pthread_t consumer_thread[N_CPU];
    pthread_attr_t thread_attr;

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
    pthread_attr_init(&thread_attr);
    res = pthread_attr_init(&thread_attr);
    if (res != 0) {
        fprintf(stderr, "[main]: Attribute creation failed.\n");
        exit(EXIT_FAILURE);
    }
    res = pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);
    if (res != 0) {
        fprintf(stderr, "[main]: Setting detached attribute failed.\n");
        exit(EXIT_FAILURE);
    }

    /* trigger balancer thread */
    res =
        pthread_create(&balancer_thread, &thread_attr, balancer, (void *) NULL);
    if (res != 0) {
        fprintf(stderr, "[main]: balancer thread creation failed.\n");
        exit(EXIT_FAILURE);
    }

    /* create 4 CPU threads */
    for (int i = 0; i < N_CPU; i++) {
        cpu_count[i] = i;
        res = pthread_create(&consumer_thread[i], &thread_attr, consumer,
                             (void *) &cpu_count[i]);
        if (res != 0) {
            fprintf(stderr, "[main]: consumer thread creation failed.\n");
            exit(EXIT_FAILURE);
        }
    }
    (void) pthread_attr_destroy(&thread_attr);
    while (running) {
    }
    return 0;
}

/* Producer implementation.
 * The thread generates processes distributed into the queue of each comsumers.
 * Initially, read processes infos from a file name defined in PROCESS_FILE.
 * Generates 20 processes: 12 SCHED_NORMAL, 4 SCHED_FIFO, 4 SCHED_RR
 * Each run queue will has 5 process intitally
 */
void *producer(void *arg)
{
    FILE *file;                // file descriptor
    char line[100];            // store a line of the text file
    struct process_t process;  // store
    int CPU, res;
    int count = 1;
    printf("[producer]: Producer has been created.\n");
    printf("[producer]: Loading processes. Please wait a few secs....\n");
    file = fopen(PROCESS_FILE, "r");
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
        /* b_value is the number of processes each CPU should has so that
         * all CPUs has the same number of processes
         */
        b_value = n_process / N_CPU;
    }

    /* Initially, each CPU has 0 process in the queue */
    for (int i = 0; i < N_CPU; i++) {
        p_count[i] = 0;
    }

    /* Initialize the queue for each CPU */
    for (int i = 0; i < N_CPU; i++) {
        for (int j = 0; j < N_QUEUE; j++) {
            init_queue(&RQ[i][j]);  // init queue
            /* initialize queue mutex */
            res = pthread_mutex_init(&QM[i][j], NULL);
            if (res != 0) {
                fprintf(stderr, "[Producer]: Mutex initialization failed.\n");
                exit(EXIT_FAILURE);
            }
        }
    }

    /* Read and distribute proccess into ready queue for each consumers */
    while (1) {
        if (!fgets(line, 100, file)) /* reach EOF */
            break;

        process = ltop(line);  /* convert string into process */
        process.pid = count++; /* assign pid to process */
        process.prio = process.static_prio;
        process.time_slice = 0;
        process.accu_time_slice = 0;
        process.sleep_avg = 0;

        /* find an available CPU to assign the process into */
        do {
            srand(time(NULL));  /* seed the random generator */
            CPU = (rand() % 4); /* generate a random CPU number 0 - 3 */
        } while (p_count[CPU] >= b_value);
        /* assign the process into the CPU queue */
        process.last_cpu = CPU;

        /* If process is SCHED_NORMAL, then store in RQ1.
         */
        if (process.sched == NORMAL) {
            pthread_mutex_lock(&QM[CPU][1]);
            append(&process, &RQ[CPU][1]);
            pthread_mutex_unlock(&QM[CPU][1]);
        } else { /* store process in RQ0 */
            pthread_mutex_lock(&QM[CPU][0]);
            append(&process, &RQ[CPU][0]);
            pthread_mutex_unlock(&QM[CPU][0]);
        }
        p_count[CPU]++; /* increase the number of processes for that CPU */
    }
    process_status();
    sleep(2);
    pthread_exit(NULL);
}

/* Convert a process information string into a process struct.
 * @param string containing the process info
 * @return process_t containing process info extracted from the string
 */
struct process_t ltop(char *line)
{
    struct process_t process;
    char *token;
    char delimiter[] = " ,";
    token = strtok(line, delimiter); /* extract the SCHED info */

    /* transfer SCHED string into process.sched */
    if (strcmp(token, sched_to_string[RR]) == 0)
        process.sched = RR; /* Round-Robin */
    if (strcmp(token, sched_to_string[FIFO]) == 0)
        process.sched = FIFO; /* FIFO */
    if (strcmp(token, sched_to_string[NORMAL]) == 0)
        process.sched = NORMAL; /* normal */

    token = strtok(NULL, delimiter); /* extract the static_prio */
    process.static_prio = atoi(token);
    token = strtok(NULL, delimiter); /* extract the execution time */
    process.exec_time = atoi(token);
    return process;
}

/* Print out to the console the table containing all processes */
void process_status(void)
{
    struct process_t process;
    struct queue_t queue;
    struct queue_t RQ_snapshot[N_CPU][N_QUEUE];

    /* take a snapshot of the queues */
    memcpy(&RQ_snapshot, &RQ, sizeof(RQ));
    printf("%-3s | %-12s | %-3s | %-8s | %-11s | %-4s | %-9s \n", "PID",
           "SCHED_SCHEME", "CPU", "last_CPU", "static_prio", "prio",
           "exec_time");
    for (int i = 0; i < N_CPU; i++)
        for (int j = 0; j < N_QUEUE; j++) {
            queue = RQ_snapshot[i][j];
            for (int k = queue.out; k < queue.in; k++) {
                process = queue.pool[k];
                printf("%-3d | %-12s | %-3d | %-8d | %-11d | %-4d | %-9d \n",
                       process.pid, sched_to_string[process.sched], i,
                       process.last_cpu, process.static_prio, process.prio,
                       process.exec_time);
            }
        }
}

/* Consumer implementation.
 * When created, the *arg tell the thread its CPU number (From 0 to 3)
 * The thread execute the processes in its own ready queue.
 * The processes has 3 queues: RQ0, RQ1, RQ3.
 */
void *consumer(void *arg)
{
    int CPU = *(int *) arg; /* CPU number assigned from for this consumer */
    struct process_t *process;
    printf("[CPU %i]: CPU %i thread has been created.\n", CPU, CPU);
    while (running) {
        for (int i = 0; i < N_QUEUE; i++) { /* go through each queue */
            while (running) { /* continuously executes processes in queue */
                /* take a process out of the queue */
                pthread_mutex_lock(&QM[CPU][i]);
                process = take(&RQ[CPU][i]);
                pthread_mutex_unlock(&QM[CPU][i]);
                /* go to next queue if there is no more process in queue */
                if (!process)
                    break;

                /* print out process info */
                printf(
                    "[CPU %d]: process PID %d is selected,%s, priority = %d.\n",
                    CPU, process->pid, sched_to_string[process->sched],
                    process->prio);

                /* execute the process based on its scheduling schem */
                switch (process->sched) {
                case FIFO:
                    /* execute SCHED_FIFO process */
                    execute_FIFO(CPU, process);
                    break;
                case RR:
                    /* execute SCHED_RR process */
                    execute_RR(CPU, process);
                    break;
                case NORMAL:
                    execute_NORMAL(CPU, process);
                    /* execute SCHED_NORMAL process */
                    break;
                default:
                    /* Invalid process */
                    printf(
                        "[CPU %d]: PROCESS PID %d has invalid scheduling "
                        "scheme.\n",
                        CPU, process->pid);
                }
            }
        }
    }
    pthread_exit(NULL);
}

/* called by the CPU to execute a SCHED_FIFO process */
static void execute_FIFO(int CPU, struct process_t *process)
{
    /* a FIFO process run to completion */
    printf("[CPU %d]: executing process PID %d, %s.\n", CPU, process->pid,
           sched_to_string[process->sched]);
    usleep(process->exec_time * mili_to_micro);
    printf("[CPU %d]: process PID %d is finished.\n", CPU, process->pid);
    process_status();
    /* update counters */
    p_count[CPU]--; /* reduce number of processes this CPU has */
    n_process--;    /* reduce total number of processes still in the queue */
}

/* called by the CPU to execute a SCHED_RR process */
static void execute_RR(int CPU, struct process_t *process)
{
    int remaining_exec_time;
    printf("[CPU %d]: executing process PID %d, %s.\n", CPU, process->pid,
           sched_to_string[process->sched]);

    /* Calculate time quantum for the process */
    if (process->prio < 120)
        process->time_slice = (140 - process->prio) * 20;
    else
        process->time_slice = (140 - process->prio) * 5;

    /* Calculate the remaining execution time of the process */
    remaining_exec_time = process->exec_time - process->accu_time_slice;

    /* Accumulate time_slice */
    process->accu_time_slice += process->time_slice;

    /* execute process */
    if (remaining_exec_time > process->time_slice) {
        usleep((process->time_slice + sleep_time) * mili_to_micro);
        printf(
            "[CPU %d]: process PID %d is preempted. Remaining execution time = "
            "%d.\n",
            CPU, process->pid, process->exec_time - process->accu_time_slice);
        /* Put back the process into the queue RQ0 */
        pthread_mutex_lock(&QM[CPU][0]);
        append(process, &RQ[CPU][0]);
        pthread_mutex_unlock(&QM[CPU][0]);
    } else {
        usleep((remaining_exec_time + sleep_time) * mili_to_micro);
        printf("[CPU %d]: process PID %d is finished.\n", CPU, process->pid);
        process_status();
        /* update counters */
        p_count[CPU]--; /* reduce number of processes this CPU has */
        n_process--; /* reduce total number of processes still in the queue */
    }
}

/* called by the CPU to execute a SCHED_NORMAL process */
static void execute_NORMAL(int CPU, struct process_t *process)
{
    struct timeval t1, t2;
    int deltaT, ticks;
    int service_time, remaining_exec_time;
    /* Calculate the time ticks */
    t1 = process->last_run;
    gettimeofday(&t2, NULL);
    deltaT = (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_usec - t1.tv_usec) / 1000;
    ticks = deltaT / 200;
    ticks = (ticks < MAX_SLEEP_AVG) ? ticks : MAX_SLEEP_AVG;

    /* add tick to the sleep_avg */
    process->sleep_avg += ticks;
    process->sleep_avg = (process->sleep_avg < MAX_SLEEP_AVG)
                             ? process->sleep_avg
                             : MAX_SLEEP_AVG;

    /* Calculate the dynamic priority */
    process->prio = MAX(100, MIN(process->prio - process->sleep_avg + 5, 139));

    /* Calculate time quantum for the process */
    if (process->prio < 120)
        process->time_slice = (140 - process->prio) * 20;
    else
        process->time_slice = (140 - process->prio) * 5;

    /* Calculate the remaining execution time of the process */
    remaining_exec_time = process->exec_time - process->accu_time_slice;

    /* Accumulate time_slice */
    process->accu_time_slice += process->time_slice;

    /* service_time is between 10 ms and time_slice */
    srand(time(NULL));  // seed the random generator
    service_time = 10 + (rand() % (process->time_slice - 9));

    /* execute process */
    printf("[CPU %d]: executing process PID %d, %s, service_time = %d.\n", CPU,
           process->pid, sched_to_string[process->sched], service_time);
    if (remaining_exec_time > service_time) {
        /* Record the time before executing the process */
        gettimeofday(&t1, NULL);

        /* execute the process */
        usleep((service_time + sleep_time) * mili_to_micro);

        /* Record the time after executing the process */
        gettimeofday(&t2, NULL);

        /* Record the last_run */
        process->last_run = t2;

        /* Calculate the execution time */
        deltaT =
            (t2.tv_sec - t1.tv_sec) * 1000 + (t2.tv_usec - t1.tv_usec) / 1000;
        deltaT = deltaT - sleep_time;  // substract the artificial sleep_time
        ticks = deltaT / 100;

        /* Substract the ticks from sleep_avg */
        process->sleep_avg = process->sleep_avg - ticks;
        if (service_time <
            process->time_slice) {  // if the process is waiting for I/O event
            printf(
                "[CPU %d]: process PID %d is blocked waiting for an event.\n",
                CPU, process->pid);
            usleep(wait_time * mili_to_micro);  // simulate the blocked time
        }
        printf(
            "[CPU %d]: process PID %d is preempted. Remaining execution time = "
            "%d.\n",
            CPU, process->pid, process->exec_time - process->accu_time_slice);

        /* Put back the process into the queue */
        if (process->prio < 130) { /* if prio < 130, process go to queue 1 */
            pthread_mutex_lock(&QM[CPU][1]);
            append(process, &RQ[CPU][1]);
            pthread_mutex_unlock(&QM[CPU][1]);
        } else { /* if prio >= 130, process go to queue 2 */
            pthread_mutex_lock(&QM[CPU][2]);
            append(process, &RQ[CPU][2]);
            pthread_mutex_unlock(&QM[CPU][2]);
        }
    } else {
        usleep((remaining_exec_time + sleep_time) * mili_to_micro);
        printf("[CPU %d]: process PID %d is finished.\n", CPU, process->pid);
        process_status();
        /* update counters */
        p_count[CPU]--; /* reduce number of processes this CPU has */
        n_process--;    /* reduce total number of processes still in queue */
    }
}

/* The thread balance the number of queues distributed for each consumers. */
void *balancer(void *arg)
{
    struct process_t *process;
    int current_queue;
    printf("[balancer]: Balancer has been created.\n");
    while (running) {
        if (n_process <= 0) { /* check if there are no more process */
            printf(
                "[Balancer] No more process to execute. The emulator stop.\n");
            running = 0;
            break;
        }

        /* update the b_value */
        /* b_value is the number of processes each CPU should have so that
        ** processes are distributed equally to all CPUs */
        b_value = n_process / N_CPU;
        for (int i = 0; i < N_CPU; i++) {
            while (p_count[i] > b_value) { /* more than b_value processes */
                /* take out one process from the CPU's queue */
                process = NULL;
                int j = 0;
                for (j = N_QUEUE - 1; j >= 0; j--) {
                    pthread_mutex_lock(&QM[i][j]);
                    process = take(&RQ[i][j]);
                    pthread_mutex_unlock(&QM[i][j]);

                    /* update p_count */
                    p_count[i]--;
                    if (process) {
                        current_queue = j;
                        break;
                    }
                }
                /* if cannot take out any process then break */
                if (!process)
                    break;

                /* find a CPU to move the process over */
                for (j = 0; j < N_CPU; j++) {
                    if (j != i && p_count[j] < b_value)
                        break;
                }
                /* if cannot find any available cpu, then break */
                if (j == N_CPU || p_count[j] >= b_value) {
                    /* put back the process into the queue */
                    pthread_mutex_lock(&QM[i][current_queue]);
                    append(process, &RQ[i][current_queue]);
                    pthread_mutex_unlock(&QM[i][current_queue]);
                    p_count[i]++;
                    break;
                }

                /* found a new cpu for process */
                int new_cpu = j;

                /* update process info */
                process->last_cpu = i;
                printf(
                    "[Balancer]: process PID %d is moved from CPU %d to CPU "
                    "%d.\n",
                    process->pid, i, j);

                /* add process into new cpu's queue
                 * process is SCHED_NORMAL then store in RQ1
                 */
                if (process->sched == NORMAL) {
                    pthread_mutex_lock(&QM[new_cpu][1]);
                    append(process, &RQ[new_cpu][1]);
                    pthread_mutex_unlock(&QM[new_cpu][1]);
                } else { /* store process in RQ0 */
                    pthread_mutex_lock(&QM[new_cpu][0]);
                    append(process, &RQ[new_cpu][0]);
                    pthread_mutex_unlock(&QM[new_cpu][0]);
                }
                /* update p_count */
                p_count[new_cpu]++;
            }
        }
        sleep(b_freq);
    }
    pthread_exit(NULL);
}
