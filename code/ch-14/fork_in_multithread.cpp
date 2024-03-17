/**
 * @file fork_in_multithread.cpp
 * @author 
 * @date 2024-03-15
 * @brief 在多线程程序中调用 fork 函数。
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

int main() {
    pthread_mutex_init(&mutex, nullptr);
    pthread_t id;
    pthread_create(&id, nullptr, another, nullptr);
    // 在父进程中的主线程暂停1s，以确保在执行fork操作之前，子线程已经开始运行并
    // 获得了互斥变量 mutex。
    sleep(1);
    int pid = fork();
    if (pid < 0) {
        pthread_join(id, nullptr);
        pthread_mutex_destroy(&mutex);
        return 1;
    } else if (pid == 0) {
        printf("I am in the child, want to get the lock\n");
        // 子进程从父进程继承了互斥锁 mutex 的状态，该互斥锁处于锁住的状态，
        // 这是由父进程中的子线程执行 pthread_mutex_lock 引起的。因此，下面的
        // 加锁操作会一直阻塞下去。但是，逻辑上来讲，此处加锁不应该阻塞。
        pthread_mutex_lock(&mutex);
        printf("I can not run to here, oop...\n");
        pthread_mutex_unlock(&mutex);
        exit(0);
    } else {
        wait(nullptr);
    }
    pthread_join(id, nullptr);
    pthread_mutex_destroy(&mutex);
    return 0;
}