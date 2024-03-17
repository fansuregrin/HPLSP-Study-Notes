/**
 * @file sig_handling.cpp
 * @author 
 * @date 2024-03-15
 * @brief 用一个线程来处理所有信号。
*/
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

#define handle_error_en(en, msg) \
    do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

static void * sig_thread(void * arg) {
    sigset_t *set = (sigset_t *)arg;
    int s, sig;
    for (;;) {
        s = sigwait(set, &sig);
        if (s != 0) handle_error_en(s, "sigwait");
        printf("Signal handling thread got signal %d\n", sig);
    }
}

int main() {
    pthread_t thread;
    sigset_t set;
    int s;

    sigemptyset(&set);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGUSR1);
    s = pthread_sigmask(SIG_BLOCK, &set, nullptr);
    if (s != 0) handle_error_en(s, "pthread_sigmask");

    s = pthread_create(&thread, nullptr, sig_thread, (void *)&set);
    if (s != 0) handle_error_en(s, "pthread_create");
}