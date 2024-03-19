/**
 * @file web_server.cpp
 * @author
 * @date 2024-03-18
 * @brief WEB服务器主程序
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>

#include "../ch-14/locker.h"
#include "http_conn.h"
#include "threadpool.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int add_fd(int epollfd, int fd, bool one_shot);
extern void remove_fd(int epollfd, int fd);

/**
 * @brief 添加信号
 * @param sig 要添加的信号
 * @param handler 信号处理器函数
 * @param restart 被信号中断后是否自动重启
*/
static void add_sig(int sig, void(*handler)(int), bool restart=true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

/**
 * @brief 
*/
void show_error(int connfd, const char * info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char * argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    // 忽略 SGIPIPE 信号
    add_sig(SIGPIPE, SIG_IGN);

    // 创建线程池
    threadpool<http_conn> * pool = nullptr;
    try {
        pool = new threadpool<http_conn>;
    } catch (...) {
        return 1;
    }

    http_conn * users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = bind(listenfd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    add_fd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
    
    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(listenfd, (sockaddr *)&client_addr,
                                &client_addr_len);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy\n");
                    continue;
                }
                users[connfd].init(connfd, client_addr);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                if (users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn();
                }
            } else {
                
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;
    return 0;
}