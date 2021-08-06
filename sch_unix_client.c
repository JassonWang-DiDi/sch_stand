#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
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

static struct {
    int cpu;
    int policy;
    int priority;
    char name[108];
    int running;
} g_param;

void sig_stop(int sig)
{
    (void)sig;
    g_param.running = 0;
}

void print_usage(const char* cmdline)
{
    printf ("%s -c cpuset [-f pp/-r pp/-n] -s sss.sock", cmdline);
    printf ("-c cpuset  : cpuset bitmask, default for ALL. eg. 3 for cpu0 & cpu1");
    printf ("-f pp      : SCHED_FIFO with pp priority");
    printf ("-r pp      : SCHED_RR with pp priority");
    printf ("-n         : SCHED_OTHER priority, default");
    printf ("-s sss.sock: Server bind sock, sch_unix.sock for default");
}

int get_param(int argc, const void** argv)
{
    memset(&g_param, 0, sizeof(g_param));
    g_param.policy = SCHED_OTHER;
    g_param.priority = 0;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-c") == 0)) {
            g_param.cpu = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-f") == 0)) {
            g_param.policy = SCHED_FIFO;
            g_param.priority = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-r") == 0)) {
            g_param.policy = SCHED_RR;
            g_param.priority = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "-s") == 0)) {
            strcpy(g_param.name, argv[++i]);
        } else {
            continue;
        }
    }

    return 0;
}

int main(int argc, const void** argv)
{
    signal(SIGINT, sig_stop);
    get_param(argc, argv);

    if (g_param.policy != SCHED_OTHER) {
        struct sched_param sp;
        sp.sched_priority = g_param.priority;
        pthread_setschedparam(pthread_self(), g_param.policy, &sp);
    }

    if (g_param.cpu != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < (sizeof(g_param.cpu)*8); i++) {
            CPU_SET(i, &cpuset);
        }
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, g_param.name);

    g_param.running = 1;
    while (g_param.running) {
        /* code */
        struct timeval tv[2];
        memset(&tv[0], 0, sizeof(tv[0]));
        gettimeofday(&tv[0], NULL);
        int size = sendto(sock, &tv[0], sizeof(tv[0]), 0, (const struct sockaddr*)&addr, len);
        // printf ("Socket send data(%dB)\n", size);
        //sleep(1);
        for (int i = 0; i < 1000000; i++) {
            if (i == 100000) {
                break;
            }
        }
    }

    close(sock);
    
    return 0;
}
