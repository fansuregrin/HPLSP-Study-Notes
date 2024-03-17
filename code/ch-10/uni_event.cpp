/**
 * @file uni_event.cpp
 * @author
 * @date 2024-03-10
 * @brief 统一事件源。
*/
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <cstdlib>

#define MAX_EVENT_NUMBER 1024

static int pipefd[2];

int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void add_fd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

// 信号处理函数
void signal_handler(int sig) {
    // 保存原来的errno，在函数最后恢复，以保证函数的可重入性
    int save_errno = errno;
    int msg = sig;
    // 将信号值写入管道，以通知主循环
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号的处理函数
void add_sig(int sig) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

int main(int argc, char * argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    
    ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    if (ret < 0) {
        printf("errno is: %d\n", errno);
        return 1;
    }
    
    ret = listen(listen_fd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    add_fd(epollfd, listen_fd);

    // 使用socketpair创建管道，注册pipefd[0]上的可读事件
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    set_nonblocking(pipefd[1]);
    add_fd(epollfd, pipefd[0]);

    // 设置一些信号的处理函数
    add_sig(SIGHUP);
    add_sig(SIGCHLD);
    add_sig(SIGTERM);
    add_sig(SIGINT);

    bool stop_server = false;

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listen_fd) {
                sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(listen_fd, (sockaddr *)&client_address,
                            &client_addr_len);
                add_fd(epollfd, connfd);
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(sockfd, signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i=0; i<ret; ++i) {
                        switch (signals[i]) {
                            case SIGCHLD:
                            case SIGHUP: {
                                continue;
                            }
                            case SIGTERM:
                            case SIGINT: {
                                stop_server = true;
                            }
                        }
                    }
                }
            } else {

            }
        }
    }

    printf("close fds\n");
    close(epollfd);
    close(listen_fd);
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
}