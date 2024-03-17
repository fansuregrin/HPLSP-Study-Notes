/**
 * @file fork_in_multithread.cpp
 * @author 
 * @date 2024-03-15
 * @brief 在多线程程序中调用 fork 函数, 并用 pthread_atfork 确保父子进程有清楚的锁状态。
*/
#include <pthread.h>
#include <unistd.h>
#include <wait.h>
#include <cstdlib>
#include <cstdio>

pthread_mutex_t mutex;

void * another(void * arg) {
    printf("in child thread, lock the mutex\n");
    pthread_mutex_lock(&mutex);
    sleep(5);
    pthread_mutex_unlock(&mutex);
}

/**
 * @brief 在子进程被创建出来之前，锁住父进程中的互斥锁。
*/
void prepare() {
    pthread_mutex_lock(&mutex);
}

/**
 * @brief 在子进程被创建之后和fork返回之前，释放prepare中被锁住的互斥锁。
*/
void infork() {
    pthread_mutex_unlock(&mutex);
}

int main() {
    pthread_mutex_init(&mutex, nullptr);
    pthread_t id;
    pthread_create(&id, nullptr, another, nullptr);
    // 在父进程中的主线程暂停1s，以确保在执行fork操作之前，子线程已经开始运行并
    // 获得了互斥变量 mutex。
    sleep(1);
    pthread_atfork(prepare, infork, infork);
    int pid = fork();
    if (pid < 0) {
        pthread_join(id, nullptr);
        pthread_mutex_destroy(&mutex);
        return 1;
    } else if (pid == 0) {
        printf("I am in the child, want to get the lock\n");
        pthread_mutex_lock(&mutex);
        printf("I can run to here, yeah~\n");
        pthread_mutex_unlock(&mutex);
        exit(0);
    } else {
        wait(nullptr);
    }
    pthread_join(id, nullptr);
    pthread_mutex_destroy(&mutex);
    return 0;
}