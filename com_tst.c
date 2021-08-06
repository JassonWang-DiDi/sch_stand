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
#include <sys/prctl.h>

#define MAX_TASK_COUNT      100
#define DEFAULT_LOOP_CYCLE  1000000
const char *master_sock_name = "/tmp/com_tst_master.sock";
const char *task_sock_name = "/tmp/com_tst_%d.sock";

typedef enum _master_cmd_t {
    MASTER_CMD_NONE,
    MASTER_CMD_INITED,
    MASTER_CMD_FINISH,
    MASTER_CMD_START,
    MASTER_CMD_LOOP,
    MASTER_CMD_STOP
} master_cmd_t;

typedef struct _master_packet_t {
    master_cmd_t cmd;
    int id;
    char data[64];
} master_packet_t;

typedef enum _test_mode_t {
    TEST_MODE_NONE,
    TEST_MODE_LOOP,
    TEST_MODE_P2P
} test_mode_t;

typedef enum _task_mode_t {
    TASK_MODE_THREAD,
    TASK_MODE_PROCESS
} task_mode_t;

typedef enum _com_mode_t {
    COM_MODE_SOCKET,
    COM_MODE_SEMAPHORE
} com_mode_t;

typedef enum _work_mode_t {
    WORK_MODE_SEND,
    WORK_MODE_RECV,
    WORK_MODE_LOOP,
} work_mode_t;

typedef struct _task_data_t {
    int cpu;
    int policy;
    int priority;
    work_mode_t work_mode;
    task_mode_t task_mode;
    com_mode_t com_mode;
    struct {
        struct sockaddr_un addr;
        socklen_t len;
        int sock;
    } sock_data;
    struct {
        sem_t* sem;
    } sem_data;
    struct {
        sem_t* sem_pub;
        struct sockaddr_un* addr_pub;
    } loop_data;
    int inited;
    int finished;
    pthread_t pth;
} task_data_t;

static struct {
    task_data_t task[MAX_TASK_COUNT];
    test_mode_t test_mode;
    task_mode_t task_mode;
    com_mode_t com_mode;
    long cycle;
    int task_cnt;
    int running;
    sem_t* sems;
    struct {
        struct sockaddr_un addr;
        int sock;
    } master;
} g_param;

void sig_stop(int sig)
{
    (void)sig;
    g_param.running = 0;
}

void print_usage(const char* cmdline)
{
    printf ("%s [-d -r/-l] pp@cpuset [-t/-p] [-u/-s] -c cycle\n", cmdline);
    printf ("-d pp@cpuset ... : Send using RR TASK with priority pp and cpuset.\n");
    printf ("-r pp@cpuset ... : Recv using RR TASK with priority pp and cpuset.\n");
    printf ("-l pp@cpuset ... : Loop using RR TASK with priority pp and cpuset.\n");
    printf ("-t               : Using multi-thread model. Default mode\n");
    printf ("-p               : Using multi-process model.\n");
    printf ("-u               : Using Unix-UDP-socket. Default mode.\n");
    printf ("-s               : Using Semaphore.\n");
    printf ("-c cycle         : Cycle count. Default DEFAULT_LOOP_CYCLE.\n");
}

int get_ppcpu(task_data_t* task, const char* ppcpu, work_mode_t work)
{
    int s = 0;
    int pp = 0;
    int cpu = 0;
    const char* ptr = ppcpu;

    // Check str
    while (*ptr != '\0') {
        if ((*ptr >= '0') && (*ptr <= '9')) {
            if (s == 0) {
                pp = pp * 10 + (*ptr - '0');
            } else if (s == 1) {
                cpu = cpu * 10 + (*ptr - '0');
            }
        } else if (*ptr == '@') {
            s = 1;
        } else {
            s = -1;
            break;
        }
        ptr ++;
    }

    if (s == -1) {
        return -1;
    } else {
        task->cpu = cpu;
        task->policy = SCHED_RR;
        task->priority = pp;
        task->work_mode = work;
        task->sem_data.sem = NULL;
        return 0;
    }
}

int get_param(int argc, const void** argv)
{
    int ret = -1;
    memset(&g_param, 0, sizeof(g_param));
    g_param.cycle = DEFAULT_LOOP_CYCLE;

    for (int i = 1; i < argc; i++) {
        if ((strcmp((const char*)argv[i], "-d") == 0)) {
            for (int j = i+1; j < argc; j++) {
                if (get_ppcpu(&g_param.task[g_param.task_cnt], (const char*)argv[j], WORK_MODE_SEND) == 0) {
                    g_param.task_cnt++;
                    i++;
                } else {
                    break;
                }
            }
            if (g_param.test_mode == TEST_MODE_LOOP) {
                ret = -2;
                break;
            }
            g_param.test_mode = TEST_MODE_P2P;
        } else if ((strcmp((const char*)argv[i], "-r") == 0)) {
            for (int j = i+1; j < argc; j++) {
                if (get_ppcpu(&g_param.task[g_param.task_cnt], (const char*)argv[j], WORK_MODE_RECV) == 0) {
                    g_param.task_cnt++;
                    i++;
                } else {
                    break;
                }
            }
            if (g_param.test_mode == TEST_MODE_LOOP) {
                ret = -3;
                break;
            }
            g_param.test_mode = TEST_MODE_P2P;
        } else if ((strcmp((const char*)argv[i], "-l") == 0)) {
            for (int j = i+1; j < argc; j++) {
                if (get_ppcpu(&g_param.task[g_param.task_cnt], (const char*)argv[j], WORK_MODE_LOOP) == 0) {
                    g_param.task_cnt++;
                    i++;
                } else {
                    break;
                }
            }
            if (g_param.test_mode == TEST_MODE_P2P) {
                ret = -4;
                break;
            }
            g_param.test_mode = TEST_MODE_LOOP;
        } else if ((strcmp((const char*)argv[i], "-t") == 0)) {
            g_param.task_mode = TASK_MODE_THREAD;
        } else if ((strcmp((const char*)argv[i], "-p") == 0)) {
            g_param.task_mode = TASK_MODE_PROCESS;
        } else if ((strcmp((const char*)argv[i], "-u") == 0)) {
            g_param.com_mode = COM_MODE_SOCKET;
        } else if ((strcmp((const char*)argv[i], "-s") == 0)) {
            g_param.com_mode = COM_MODE_SEMAPHORE;
        } else if ((strcmp((const char*)argv[i], "-c") == 0)) {
            g_param.cycle = atol((const char*)argv[++i]);
            if (g_param.cycle == 0) {
                ret = -5;
                break;
            }
        }
    }

    if ((ret == 0) && ((argc == 1))) {
        ret = -2;
    }

    if (ret == -1) {
        printf ("start %d task\n", g_param.task_cnt);
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n\n", __func__, __LINE__, ret, errno);
        print_usage((const char*)argv[0]);
    }

    return ret;
}

int do_init_socket(task_data_t* task, task_mode_t tmode)
{
    int ret = -1;

    // Create socket
    task->sock_data.sock = -1;
    remove(task->sock_data.addr.sun_path);
    task->sock_data.sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (task->sock_data.sock < 0) {
        ret = -2;
    }

    // Bind to local file
    if (ret == -1) {
        int r = bind(task->sock_data.sock,
                    (const struct sockaddr*)&task->sock_data.addr,
                    task->sock_data.len);
        if (r < 0) {
            ret = -3;
        }
    }

    // Config select files

    // Clear context
    if (ret < -1) {
        if (task->sock_data.sock >= 0) {
            close(task->sock_data.sock);
            task->sock_data.sock = -1;
        }
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    } else {
        ret = 0;
    }

    return ret;
}

int do_init_semaphore(task_data_t* task, task_mode_t tmode)
{
    int ret = -1;
    int r;
    int shared = (tmode == TASK_MODE_THREAD) ? 0 : 1;

    // Init semaphore
    r = sem_init(task->sem_data.sem, shared, 0);
    if (r < 0) {
        ret = -2;
    }

    // Clear context
    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_task_loop(task_data_t* task, int id)
{
    int ret = -1;
    int r;

    if (g_param.com_mode == COM_MODE_SEMAPHORE) {
        volatile long* loop = (volatile long*)&g_param.sems[g_param.task_cnt];
        struct timeval* tv = (struct timeval*)&loop[2];
        for (;;) {
            r = sem_wait(task->sem_data.sem);
            loop[1]--;
            if (loop[1] == 0) {
                struct timeval tv2;
                gettimeofday(&tv2, NULL);
                long du = (tv2.tv_sec - tv->tv_sec) * 1000000 + tv2.tv_usec - tv->tv_usec;
                printf ("Loop(%ld) stop in %ld us for %ld ns tick\n", loop[0], du, du * 1000 / loop[0]);
            }
            r = sem_post(task->loop_data.sem_pub);
            if (r < 0) {
                break;
            }
            if (loop[1] <= 0) {
                break;
            }
        }
    } else if (g_param.com_mode == COM_MODE_SOCKET) {
        master_packet_t packet;
        long* loop = (long*)packet.data;
        struct timeval* tv = (struct timeval*)&loop[2];
        for (;;) {
            recvfrom(task->sock_data.sock, &packet, sizeof(packet), 0, NULL, NULL);
            if (packet.cmd == MASTER_CMD_START) {
                packet.cmd = MASTER_CMD_LOOP;
                packet.id = id;
                loop[1] = loop[0]-1;
                gettimeofday(tv, NULL);
            } else if (packet.cmd == MASTER_CMD_LOOP) {
                if (loop[1] <= 0) {
                    // Stop
                    packet.cmd = MASTER_CMD_STOP;
                    packet.id = id;
                    struct timeval tv2;
                    gettimeofday(&tv2, NULL);
                    long du = (tv2.tv_sec - tv->tv_sec) * 1000000 + tv2.tv_usec - tv->tv_usec;
                    printf ("Loop(%ld) stop in %ld us for %ld ns tick\n", loop[0], du, du * 1000 / loop[0]);
                } else {
                    packet.cmd = MASTER_CMD_LOOP;
                    packet.id = id;
                    loop[1] = loop[1]-1;
                }
            } else if (packet.cmd == MASTER_CMD_STOP) {
                packet.cmd = MASTER_CMD_STOP;
                packet.id = id;
            }
            sendto(task->sock_data.sock, &packet, sizeof(packet), 0,
                                (const struct sockaddr*)task->loop_data.addr_pub,
                                sizeof(*task->loop_data.addr_pub));
            if (packet.cmd == MASTER_CMD_STOP) {
                break;
            }
        }
    }

    return 0;
}

int do_task_send(task_data_t* task, int id)
{
    int ret = -1;
    return ret;
}

int do_task_recv(task_data_t* task, int id)
{
    int ret = -1;
    return ret;
}

void* do_task(void* param)
{
    int id = (long)param;
    task_data_t* task = &g_param.task[id];
    long ret = -1;

    // Set task name
    char name[128];
    sprintf(name, "T%d-c%d-p%d", id, task->cpu, task->priority);
    prctl(PR_SET_NAME, name);

    // Init task policy
    if (task->policy != SCHED_OTHER) {
        struct sched_param sp;
        sp.sched_priority = task->priority;
        pthread_setschedparam(pthread_self(), task->policy, &sp);
    }

    // Init task cpuset
    if (task->cpu != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < 12; i++) {
            if ((1 << i) & task->cpu) {
                CPU_SET(i, &cpuset);
            }
        }
        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    }

    // Init com
    if (g_param.com_mode == COM_MODE_SEMAPHORE) {
        do_init_semaphore(task, g_param.task_mode);
    } else if (g_param.com_mode == COM_MODE_SOCKET) {
        do_init_socket(task, g_param.task_mode);
    }

    // Send inited packet
    master_packet_t packet;
    packet.cmd = MASTER_CMD_INITED;
    packet.id = id;
    size_t snd = sendto(g_param.master.sock, &packet, sizeof(packet), 0,
                        (const struct sockaddr*)&g_param.master.addr,
                        sizeof(g_param.master.addr));
    if (snd < 0) {
        printf ("%s@%d error %ld with errno %d\n", __func__, __LINE__, snd, errno);
    }

    // Do real task
    if (task->work_mode == WORK_MODE_LOOP) {
        do_task_loop(task, id);
    } else if (task->work_mode == WORK_MODE_RECV) {
        do_task_recv(task, id);
    } else if (task->work_mode == WORK_MODE_SEND) {
        do_task_send(task, id);
    }

    // Send finished packet
    packet.cmd = MASTER_CMD_FINISH;
    snd = sendto(g_param.master.sock, &packet, sizeof(packet), 0,
                        (const struct sockaddr*)&g_param.master.addr,
                        sizeof(g_param.master.addr));
    if (snd < 0) {
        printf ("%s@%d error %ld with errno %d\n", __func__, __LINE__, snd, errno);
    }

    return NULL;
}

int do_init_task_thread(com_mode_t com_mode, int id)
{
    int ret = -1;

    // Create thread
    int r = pthread_create(&g_param.task[id].pth, NULL, do_task, (void*)((long)id));
    if (r != 0) {
        ret = -2;
    }

    // Clear context
    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_init_task_process(com_mode_t com_mode, int id)
{
    (void)com_mode;
    // Create thread
    pid_t r = fork();
    if (r == 0) {
        // Task process
        do_task((void*)((long)id));
        return 1;
    } else if (r > 0) {
        return 0;
    } else if (r < 0) {
        printf ("%s@%d error with errno %d\n", __func__, __LINE__, errno);
        return -1;
    }
}

int do_init_task()
{
    int ret = -1;

    // Init all tasks
    for (int i = 0; i < g_param.task_cnt; i++) {
        int r = 0;
        if (g_param.task_mode == TASK_MODE_THREAD) {
            r = do_init_task_thread(g_param.com_mode, i);
        } else if (g_param.task_mode == TASK_MODE_PROCESS) {
            r = do_init_task_process(g_param.com_mode, i);
        }
        if (r < 0) {
            ret = -2;
            break;
        } else if (r > 0) {
            return 1;
        }
    }

    // Clear context
    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_init_com()
{
    int ret = -1;

    // Create coms
    if (g_param.com_mode == COM_MODE_SOCKET) {
        for (int i = 0; i < g_param.task_cnt; i++) {
            g_param.task[i].sock_data.addr.sun_family = AF_UNIX;
            sprintf (g_param.task[i].sock_data.addr.sun_path, "/tmp/com_tst_%d.sock", i);
            g_param.task[i].sock_data.len = sizeof(g_param.task[i].sock_data.addr);
            g_param.task[i].sock_data.sock = -1;
        }
        for (int i = 0; i < g_param.task_cnt-1; i++) {
            g_param.task[i].loop_data.addr_pub = &g_param.task[i+1].sock_data.addr;
        }
        g_param.task[g_param.task_cnt-1].loop_data.addr_pub = &g_param.task[0].sock_data.addr;
    } else if (g_param.com_mode == COM_MODE_SEMAPHORE) {
        // Alloc semaphore memory
        size_t add_bytes = 2 * sizeof(long) + sizeof (struct timeval);
        if (g_param.task_mode == TASK_MODE_THREAD) {
            g_param.sems = (sem_t*)malloc(sizeof(sem_t) * g_param.task_cnt + add_bytes);
        } else {
            key_t key = ftok("/tmp/com_tst.shm", 0);
            int shmid = shmget(key, sizeof(sem_t) * g_param.task_cnt + add_bytes, 0666|IPC_CREAT);
            g_param.sems = (sem_t*)shmat(shmid, 0, 0);
        }
        if (g_param.sems) {
            // Init semaphore lists
            for (int i = 0; i < g_param.task_cnt; i++) {
                g_param.task[i].sem_data.sem = &g_param.sems[i];
            }
            for (int i = 0; i < g_param.task_cnt-1; i++) {
                g_param.task[i].loop_data.sem_pub = &g_param.sems[i+1];
            }
            g_param.task[g_param.task_cnt-1].loop_data.sem_pub = &g_param.sems[0];
        } else {
            ret = -3;
        }
    }

    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_deinit_com()
{
    int ret = -1;

    // Create coms
    if (g_param.com_mode == COM_MODE_SOCKET) {
        // Do nothing
    } else if (g_param.com_mode == COM_MODE_SEMAPHORE) {
        // Dealloc semaphore memory
        size_t add_bytes = 2 * sizeof(long) + sizeof (struct timeval);
        if (g_param.task_mode == TASK_MODE_THREAD) {
            free(g_param.sems);
            g_param.sems = NULL;
        } else {
            shmdt(g_param.sems);
            g_param.sems = NULL;
        }
    }

    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_init_master()
{
    int ret = -1;

    remove(master_sock_name);
    g_param.master.sock = socket (AF_UNIX, SOCK_DGRAM, 0);
    if (g_param.master.sock < 0) {
        ret = -2;
    }

    // Bind to local master name
    if (ret == -1) {
        memset(&g_param.master.addr, 0, sizeof(g_param.master.addr));
        g_param.master.addr.sun_family = AF_UNIX;
        strcpy(g_param.master.addr.sun_path, master_sock_name);
        int r = bind(g_param.master.sock,
                    (const struct sockaddr*)&g_param.master.addr,
                    sizeof(g_param.master.addr));
        if (r < 0) {
            ret = -3;
        }
    }

    // Clear context
    if (ret == -1) {
        ret = 0;
    } else {
        printf ("%s@%d error %d with errno %d\n", __func__, __LINE__, ret, errno);
    }

    return ret;
}

int do_deinit_master()
{
    close(g_param.master.sock);

    return 0;
}

int wait4inited()
{
    int inited = 0;
    while (inited < g_param.task_cnt) {
        master_packet_t packet;
        size_t r = recvfrom(g_param.master.sock, &packet, sizeof(packet), 0, NULL, NULL);
        if (r < sizeof(packet.cmd)) {
            printf ("Command %d %d error\n", packet.cmd, packet.id);
        } else {
            if ((packet.cmd == MASTER_CMD_INITED) && (packet.id < g_param.task_cnt)) {
                if (g_param.task[packet.id].inited == 0) {
                    g_param.task[packet.id].inited = 1;
                    inited++;
                    printf ("Task %d inited, %d inited\n", packet.id, inited);
                }
            }
        }
    }

    return 0;
}

int start_task(long cycle)
{
    printf ("START\n");

    if (g_param.com_mode == COM_MODE_SEMAPHORE) {
        if (g_param.test_mode == TEST_MODE_LOOP) {
            volatile long* loop = (volatile long*)&g_param.sems[g_param.task_cnt];
            struct timeval* tv = (struct timeval*)&loop[2];
            loop[0] = loop[1] = cycle;
            gettimeofday(tv, NULL);
            sem_post(g_param.task[0].sem_data.sem);
        } else if (g_param.test_mode == TEST_MODE_P2P) {
            // TODO:
        }
    } else if (g_param.com_mode == COM_MODE_SOCKET) {
        if (g_param.test_mode == TEST_MODE_LOOP) {
            master_packet_t packet;
            packet.cmd = MASTER_CMD_START;
            packet.id = -1;
            long* loop = (long*)packet.data;
            struct timeval* tv = (struct timeval*)&loop[2];
            loop[0] = loop[1] = cycle;
            gettimeofday(tv, NULL);
            sendto(g_param.master.sock, &packet, sizeof(packet), 0,
                            (const struct sockaddr*)&g_param.task[0].sock_data.addr,
                            sizeof(g_param.task[0].sock_data.addr));
        } else if (g_param.test_mode == TEST_MODE_P2P) {
            // TODO:
        }
    }

    return 0;
}

int wait4finished()
{
    int finished = 0;
    while (finished < g_param.task_cnt) {
        master_packet_t packet;
        size_t r = recvfrom(g_param.master.sock, &packet, sizeof(packet), 0, NULL, NULL);
        if (r < sizeof(packet.cmd)) {
            printf ("Command %d %d error\n", packet.cmd, packet.id);
        } else {
            if ((packet.cmd == MASTER_CMD_FINISH) && (packet.id < g_param.task_cnt)) {
                if (g_param.task[packet.id].finished == 0) {
                    g_param.task[packet.id].finished = 1;
                    finished++;
                    printf ("Task %d finished, %d finished\n", packet.id, finished);
                }
            }
        }
    }

    return 0;
}

int main(int argc, const void** argv)
{
    // Set signal handler
    signal(SIGINT, sig_stop);

    // Get parameters
    if (get_param(argc, argv) != 0) {
        return 0;
    }

    // Init master enviroment
    if (do_init_master() != 0) {
        return 0;
    }

    // Init com data
    if (do_init_com() != 0) {
        return 0;
    }

    // Init tasks
    if (do_init_task() != 0) {
        return 0;
    }

    // Wait for all task inited
    wait4inited();

    // Post first task to start
    start_task(g_param.cycle);

    // Wait for all task finished
    wait4finished();

    // Deinit com data
    do_deinit_com();

    // Deinit master
    do_deinit_master();
    
    return 0;
}
