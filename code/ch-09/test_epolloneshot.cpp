/**
 * @file test_epolloneshot.cpp
 * @author
 * @date 2024-03-09
 * @brief 使用 EPOLLONESHOT 事件。
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
#include <pthread.h>

#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 10

struct fds {
    int epollfd;
    int sockfd;
};

// 把文件描述符设置为非阻塞的
int
set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将文件描述符fd上的EPOLLIN和EPOLLET注册到epollfd指示的epoll内核事件表中，
// 参数oneshot指定是否注册fd上的EPOLLONESHOT事件
void
add_fd(int epollfd, int fd, bool oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if (oneshot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

void
reset_oneshot(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void *
worker(void * arg) {
    int sockfd = ((fds *)arg)->sockfd;
    int epollfd = ((fds *)arg)->epollfd;
    printf("start new thread to receive data on fd: %d\n", sockfd);
    char buff[BUFFER_SIZE];
    memset(buff, '\0', BUFFER_SIZE);
    while (true) {
        int ret = recv(sockfd, buff, BUFFER_SIZE-1, 0);
        if (ret == 0) {
            close(sockfd);
            printf("foreigner closed the connection\n");
            break;
        } else if (ret < 0) {
            if (errno == EAGAIN) {
                reset_oneshot(epollfd, sockfd);
                printf("read later\n");
                break;
            }
        } else {
            printf("get content: %s\n", buff);
            sleep(5);
        }
    }
    printf("end thread receiving data on fd: %d\n", sockfd);
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
    add_fd(epollfd, listen_fd, false);

    while (true) {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0) {
            printf("epoll failure\n");
            break;
        }
        
        for (int i=0; i<ret; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listen_fd) {
                sockaddr_in client;
                socklen_t client_addr_len = sizeof(client);
                int conn_fd = accept(sockfd, (sockaddr *)&client, &client_addr_len);
                add_fd(epollfd, conn_fd, true);
            } else if (events[i].events & EPOLLIN) {
                pthread_t thread;
                fds fds_for_new_worker;
                fds_for_new_worker.epollfd = epollfd;
                fds_for_new_worker.sockfd = sockfd;
                pthread_create(&thread, nullptr, worker, (void *)&fds_for_new_worker);
            } else {
                printf("something else happend\n");
            }
        }
    }

    close(epollfd);
    close(listen_fd);
    return 0;
}