#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>

#define TASK_COUNT      100
#define TASK_REPORT     10000
#define TASK_ALL        1000000

static sem_t* sems = NULL;
static sem_t sem_finish;
static int tick = 0;
static int task_cnt = TASK_COUNT;
static int task_rpt = TASK_REPORT;
static int task_all = TASK_ALL;
static pthread_t* ths = NULL;

void print_usage(const char* cmdline)
{
    printf ("%s: Usage\n", cmdline);
}

void* th_func(void* param)
{
    int idx_w = (int)param;
    int idx_p = idx_w + 1;

    if (idx_p >= task_cnt) {
        idx_p = 0;
    }
    sem_t* sem_w = &sems[idx_w];
    sem_t* sem_p = &sems[idx_p];

    while(tick < task_all) {
        sem_wait(sem_w);
        if (tick++ % task_rpt == 0) {
            //printf ("%d: tick is %d\n", tick, 0);
        }
        sem_post(sem_p);
    }

    sem_post(&sem_finish);

    return NULL;
}

void sch_thread()
{
    struct timeval tv[2];

    printf("Task is initing...\n");
    sems = malloc(sizeof(sem_t) * task_cnt);
    ths = malloc(sizeof(pthread_t) * task_cnt);
    for (int i = 0; i < task_cnt; i++) {
        sem_init(&sems[i], 0, 0);
        pthread_create(&ths[i], NULL, th_func, (void*)i);
    }
    sem_init(&sem_finish, 0, 0);

    gettimeofday(&tv[0], NULL);
    sem_post(&sems[0]);

    sem_wait(&sem_finish);
    gettimeofday(&tv[1], NULL);

    long dua = (tv[1].tv_sec - tv[0].tv_sec) * 1000000 + tv[1].tv_usec - tv[0].tv_usec;

    printf("THREAD: Finish in %ld us or tick time %ld ns\n", dua, dua * 1000 / task_all);

    for (int i = 0; i < task_cnt; i++) {
        pthread_join(ths[i], NULL);
    }

    for (int i = 0; i < task_cnt; i++) {
        sem_destroy(&sems[i]);
    }

    free(sems);
    free(ths);
}

void pc_func(int id)
{
    int idx_w = id;
    int idx_p = idx_w + 1;
    int l_tick = id;

    if (idx_p >= task_cnt) {
        idx_p = 0;
    }
    sem_t* sem_w = &sems[idx_w];
    sem_t* sem_p = &sems[idx_p];

    while(l_tick < task_all) {
        sem_wait(sem_w);
        l_tick += task_cnt;
        sem_post(sem_p);
    }

    // printf("TASK %d exit\n", id);

    sem_post(&sems[task_cnt]);

    shmdt(sems);
}

pid_t sch_process()
{
    struct timeval tv[2];

    printf("Task sch_process is initing...\n");

    // Shared-memory alloc
    key_t key = ftok("/tmp/sch", 0);
    int shmid = shmget(key, sizeof(sem_t) * (task_cnt + 1), 0666|IPC_CREAT);
    sems = shmat(shmid, 0, 0);
    for (int i = 0; i < task_cnt + 1; i++) {
        sem_init(&sems[i], 1, 0);
    }
    printf ("SEM inited.\n");
    for (int i = 0; i < task_cnt; i++) {
        pid_t fpid = fork();
        if (fpid == 0) {
            pc_func(i);
            return fpid;
        }
    }
    printf ("PRO inited.\n");

    gettimeofday(&tv[0], NULL);
    sem_post(&sems[0]);
    printf ("PRO started.\n");

    sem_wait(&sems[task_cnt]);
    gettimeofday(&tv[1], NULL);

    long dua = (tv[1].tv_sec - tv[0].tv_sec) * 1000000 + tv[1].tv_usec - tv[0].tv_usec;

    printf("PROCESS: Finish in %ld us or tick time %ld ns\n", dua, dua * 1000 / task_all);

    for (int i = 0; i < task_cnt + 1; i++) {
        sem_destroy(&sems[i]);
    }

    shmdt(sems);

    return 1;
}

void sch_sem()
{
    sem_t sem;

    sem_init(&sem, 0, 0);
    for (int i = 0; i < task_all; i++) {
        sem_post(&sem);
    }
    sem_destroy(&sem);
}

void* sem_func(void* param)
{
    int idx_w = (int)param;
    sem_t* sem_w = &sems[idx_w];
    struct sched_param sp;
    sp.sched_priority = idx_w/2 + 1;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
    for (int i = 0; i < task_all / task_cnt; i++) {
        sem_wait(sem_w);
    }
    
    return NULL;
}

void sch_sems()
{
    struct timeval tv[2];

    printf("Task sch_sems is initing...\n");
    sems = malloc(sizeof(sem_t) * task_cnt);
    ths = malloc(sizeof(pthread_t) * task_cnt);
    for (int i = 0; i < task_cnt; i++) {
        sem_init(&sems[i], 0, 0);
        pthread_create(&ths[i], NULL, sem_func, (void*)i);
    }

    gettimeofday(&tv[0], NULL);
    for (int j = 0; j < task_all / task_cnt; j++) {
        struct sched_param sp;
        sp.sched_priority = 99;
        pthread_setschedparam(pthread_self(), SCHED_RR, &sp);
        for (int i = 0; i < task_cnt; i++) {
            sem_post(&sems[i]);
        }
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    }
    gettimeofday(&tv[1], NULL);

    long dua = (tv[1].tv_sec - tv[0].tv_sec) * 1000000 + tv[1].tv_usec - tv[0].tv_usec;

    printf("SEMS: Finish in %ld us or tick time %ld ns\n", dua, dua * 1000 / (task_all/task_cnt));

    for (int i = 0; i < task_cnt; i++) {
        pthread_join(ths[i], NULL);
    }

    for (int i = 0; i < task_cnt; i++) {
        sem_destroy(&sems[i]);
    }

    free(sems);
    free(ths);
}

int main(int argc, const void** argv)
{
    pid_t pid = sch_process();
    if (pid == 0) {
        return 0;
    } else {
        printf ("TASK 0 stop\n");
    }
    sch_thread();
    sch_sems();
    sch_sem();

    return 0;
}
