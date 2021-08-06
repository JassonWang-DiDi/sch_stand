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

    remove(g_param.name);
    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    struct sockaddr_un addr;
    socklen_t len = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, g_param.name);
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    g_param.running = 1;
    struct timeval back;
    long du_max = 0;
    long du_min = 0;
    long du_avg = 0;
    long cnt = 0;
    gettimeofday(&back, NULL);
    while (g_param.running) {
        /* code */
        struct timeval tv[2];
        memset(&tv[0], 0, sizeof(tv[0]));
        int size = recvfrom(sock, &tv[0], sizeof(tv[0]), 0, (struct sockaddr*)&addr, &len);
        gettimeofday(&tv[1], NULL);
        long du = (tv[1].tv_sec - tv[0].tv_sec) * 1000000 + tv[1].tv_usec - tv[0].tv_usec;
        du_max = (du_max < du) ? du : du_max;
        du_min = (du_min > du) ? du : du_min;
        du_avg += du;
        cnt++;
        long dux = (tv[1].tv_sec - back.tv_sec) * 1000000 + tv[1].tv_usec - back.tv_usec;
        if (dux >= 1000000) {
            du_avg /= cnt;
            printf ("Get %6ld data, delay: avg-%06ld min-%06ld max-%06ld\n", cnt, du_avg, du_min, du_max);
            du_max = du;
            du_min = du;
            du_avg = du;
            cnt = 1;
            back.tv_sec++;
        }
    }

    close(sock);
    
    return 0;
}
