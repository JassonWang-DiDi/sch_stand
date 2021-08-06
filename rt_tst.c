#include <stdio.h>
#define __USE_GNU
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <sys/shm.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <signal.h>

#define MAX_THREAD_COUNT        100

typedef struct _th_data {
    pthread_t th;
    pid_t pid;
    int tick[2];
    int policy;
    int priority;
} th_data;

static struct {
    int th_cnt;
    th_data thd[MAX_THREAD_COUNT];
    int running;
} g_param;

void sig_handler(int type)
{
    (void)type;

    g_param.running = 0;
}

void* th_func(void* param)
{
    th_data* thd = param;

    struct sched_param sp;
    sp.sched_priority = thd->priority;
    pthread_setschedparam(pthread_self(), thd->policy, &sp);
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    CPU_SET(3, &cpuset);
    CPU_SET(4, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    while (g_param.running) {
        thd->tick[0]++;
    }
    
    return NULL;
}

void print_usage(const char* cmdline)
{
    printf ("%s -f xx -r yy -n zz\n", cmdline);
}

int get_param(int argc, const void** argv)
{
    g_param.th_cnt = 0;

    for (int i = 1; i < argc; i++) {
        int sch;
        if ((strcmp(argv[i], "--fifo") == 0) || (strcmp(argv[i], "-f") == 0)) {
            sch = SCHED_FIFO;
        } else if ((strcmp(argv[i], "--rr") == 0) || (strcmp(argv[i], "-r") == 0)) {
            sch = SCHED_RR;
        } else if ((strcmp(argv[i], "--normal") == 0) || (strcmp(argv[i], "-n") == 0)) {
            sch = SCHED_OTHER;
        } else {
            continue;
        }
        while(++i < argc) {
            g_param.thd[g_param.th_cnt].priority = atoi(argv[i]);
            if (g_param.thd[g_param.th_cnt].priority == 0) {
                break;
            }
            g_param.thd[g_param.th_cnt].policy = sch;
            g_param.thd[g_param.th_cnt].tick[0] = 0;
            g_param.thd[g_param.th_cnt].tick[0] = 0;
            g_param.th_cnt++;
        }
        i--;
    }

    if ((argc < 2) || (g_param.th_cnt == 0)) {
        print_usage(argv[0]);
        return 1;
    } else {
        return 0;
    }
}

int main(int argc, const void** argv)
{
    cpu_set_t cpuset;

    if (get_param(argc, argv) != 0) {
        return 0;
    }

    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);

    signal(SIGINT, sig_handler);
    g_param.running = 1;

    for (int i = 0; i < (g_param.th_cnt); i++) {
        pthread_create(&g_param.thd[i].th, NULL, th_func, (void*)&g_param.thd[i]);
    }

    while (g_param.running) {
        sleep(1);
        int sch;
        for (int i = 0; i < (g_param.th_cnt); i++) {
            if (i == 0) {
                switch (g_param.thd[i].policy) {
                    case SCHED_FIFO:
                        printf ("FF[");
                        break;
                    case SCHED_RR:
                        printf ("RR[");
                        break;
                    case SCHED_OTHER:
                        printf ("N[");
                        break;
                }
            } else if (g_param.thd[i].policy != sch) {
                switch (g_param.thd[i].policy) {
                    case SCHED_FIFO:
                        printf ("]## FF[");
                        break;
                    case SCHED_RR:
                        printf ("]## RR[");
                        break;
                    case SCHED_OTHER:
                        printf ("]## N[");
                        break;
                }
            }
            printf ("\t%2d:%d", g_param.thd->priority, (g_param.thd[i].tick[0] - g_param.thd[i].tick[1]) / 1000000);
            g_param.thd[i].tick[1] = g_param.thd[i].tick[0];
            sch = g_param.thd[i].policy;
        }
        printf ("]\n");
    }

    for (int i = 0; i < (g_param.th_cnt); i++) {
        pthread_join(g_param.thd[i].th, NULL);
    }

    return 0;
}
