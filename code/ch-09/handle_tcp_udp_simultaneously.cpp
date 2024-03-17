/**
 * @file handle_tcp_udp_simultaneously.cpp
 * @author
 * @date 2024-03-10
 * @brief 同时处理TCP请求和UDP请求的回射服务器。
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

#define MAX_EVENT_NUMBER 1024
#define TCP_BUFFER_SIZE 512
#define UDP_BUFFER_SIZE 1024

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

int main(int argc, char * argv[]) {
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

    // 创建TCP socket
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    int ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listen_fd, 5);
    assert(ret != -1);

    // 创建UDP socket
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);
    int udp_fd = socket(PF_INET, SOCK_DGRAM, 0);
    assert(udp_fd >= 0);
    ret = bind(udp_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    // 注册TCP socket和UDP socket上的可读事件
    add_fd(epollfd, listen_fd);
    add_fd(epollfd, udp_fd);

    while (true) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == listen_fd) {
                sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(sockfd, (sockaddr *)&client_address,
                                &client_addr_len);
                add_fd(epollfd, connfd);
            } else if (sockfd == udp_fd) {
                char buff[UDP_BUFFER_SIZE];
                memset(buff, '\0', UDP_BUFFER_SIZE);
                sockaddr_in client_address;
                socklen_t clinet_addr_len = sizeof(client_address);
                ret = recvfrom(sockfd, buff, UDP_BUFFER_SIZE-1, 0,
                        (sockaddr *)&client_address, &clinet_addr_len);
                if (ret > 0) {
                    sendto(sockfd, buff, UDP_BUFFER_SIZE-1, 0,
                        (sockaddr *)&client_address, clinet_addr_len);
                }
            } else if (events[i].events & EPOLLIN) {
                char buff[TCP_BUFFER_SIZE];
                while (true) {
                    memset(buff, '\0', TCP_BUFFER_SIZE);
                    ret = recv(sockfd, buff, TCP_BUFFER_SIZE-1, 0);
                    if (ret < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        close(sockfd);
                        break;
                    } else if (ret == 0) {
                        close(sockfd);
                    } else {
                        send(sockfd, buff, ret, 0);
                    }
                }
            } else {
                printf("something else happened\n");
            }
        }
    }

    close(listen_fd);
    close(epollfd);
    return 0;
}