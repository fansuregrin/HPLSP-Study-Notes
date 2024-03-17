/**
 * @file test_lt_et_mode.cpp
 * @author
 * @date 2024-03-09
 * @brief LT(Level Tigger, 电平触发)和 ET(Edge Trigger, 边沿触发)模式。
*/
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <cstdlib>

#define MAX_EVENT_NUMBER 8
#define BUFFER_SIZE 10

// 把文件描述符设置为非阻塞的
int
set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将文件描述符fd上的EPOLLIN注册到epollfd指示的epoll内核事件表中，
// 参数enable_et指定是否对fd开启ET模式
void
add_fd(int epollfd, int fd, bool enable_et) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (enable_et) {
        event.events |= EPOLLET;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

// LT模式
void
lt(epoll_event * events, int number, int epollfd, int listen_fd) {
    char buff[BUFFER_SIZE];
    for (int i=0; i<number; ++i) {
        int sock_fd = events[i].data.fd;
        if (sock_fd == listen_fd) {
            sockaddr_in client;
            socklen_t client_addr_len = sizeof(client);
            int conn_fd = accept(sock_fd, (sockaddr *)&client, &client_addr_len);
            add_fd(epollfd, conn_fd, false);
        } else if (events[i].events & EPOLLIN) {
            printf("event trigger once\n");
            memset(buff, '\0', BUFFER_SIZE);
            int ret = recv(sock_fd, buff, BUFFER_SIZE-1, 0);
            if (ret <= 0) {
                close(sock_fd);
                continue;
            }
            printf("get %d byte(s) of content: %s\n", ret, buff);
        } else {
            printf("something else happend\n");
        }
    }
}

// ET模式
void
et(epoll_event * events, int number, int epollfd, int listen_fd) {
    char buff[BUFFER_SIZE];
    for (int i=0; i<number; ++i) {
        int sock_fd = events[i].data.fd;
        if (sock_fd == listen_fd) {
            sockaddr_in client;
            socklen_t client_addr_len = sizeof(client);
            int conn_fd = accept(sock_fd, (sockaddr *)&client, &client_addr_len);
            add_fd(epollfd, conn_fd, true);
        } else if (events[i].events & EPOLLIN) {
            // 这段代码不会被重复触发，因此需要将socket读缓冲中的数据全部读出
            printf("event trigger once\n");
            while (true) {
                memset(buff, '\0', BUFFER_SIZE);
                int ret = recv(sock_fd, buff, BUFFER_SIZE-1, 0);
                if (ret < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        printf("read later\n");
                        break;
                    }
                    close(sock_fd);
                    break;
                } else if (ret == 0) {
                    close(sock_fd);
                } else {
                    printf("get %d byte(s) of content: %s\n", ret, buff);
                }
            }
        } else {
            printf("something else happed\n");
        }
    }
}

int
main(int argc, char * argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    int ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listen_fd, 5);
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    add_fd(epollfd, listen_fd, true);

    while (true) {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0) {
            printf("epoll failure\n");
            break;
        }
        // lt(events, MAX_EVENT_NUMBER, epollfd, listen_fd);
        et(events, MAX_EVENT_NUMBER, epollfd, listen_fd);
    }

    close(epollfd);
    close(listen_fd);
    return 0;
}