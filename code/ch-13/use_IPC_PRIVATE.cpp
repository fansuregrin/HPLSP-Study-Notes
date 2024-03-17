/**
 * @file use_IPC_PRIVATE.cpp
 * @author
 * @date 2024-03-13
 * @brief 使用 IPC_PRIVATE 信号量
*/
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>  // fork, sleep
#include <cstdlib>
#include <cstdio>  // printf

union semun {
    int val;
    struct semid_ds * buf;
    unsigned short * array;
    struct seminfo * __buf;
};

// op 为-1执行 P 操作；op 为1执行 V 操作
void pv(int sem_id, int op) {
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = op;
    sem_b.sem_flg = SEM_UNDO;
    semop(sem_id, &sem_b, 1);
}

int main() {
    int sem_id = semget(IPC_PRIVATE, 1, 0666);

    union semun sem_un;
    sem_un.val = 1;
    semctl(sem_id, 0, SETVAL, sem_un);

    pid_t id = fork();
    if (id < 0) {
        return 1;
    } else if (id == 0) {
        printf("child try to get binary sem\n");
        pv(sem_id, -1);
        printf("child get the sem and would release it after 15 seconds\n");
        sleep(15);
        pv(sem_id, 1);
        exit(0);
    } else {
        printf("parent try to get binary sem\n");
        pv(sem_id, -1);
        printf("parent get the sem and would release it after 5 seconds\n");
        sleep(5);
        pv(sem_id, 1);
    }

    waitpid(id, nullptr, 0);
    semctl(sem_id, 0, IPC_RMID, sem_un);
    return 0;
}