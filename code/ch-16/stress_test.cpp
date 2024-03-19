/**
 * @file stress_test.cpp
 * @author
 * @date 2024-03-19
 * @brief 服务器压力测试程序
*/
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdlib>

static const int MAX_EVENT_NUMBER = 10000;
static const int EPOLL_WAIT_TIMEOUT = 2000;  // milli-second
static const int BUFFER_SIZE = 2048;
static const char * request = "GET /hi.html HTTP/1.1\r\nConnection: keep-alive\r\n\r\n";

/**
 * @brief 将指定文件描述符设为非阻塞的
 * @param fd 文件描述符
 * @return 原来的文件状态标志
*/
int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * @brief 为指定的文件描述符注册事件
 * @param epollfd 标识 epoll 内核事件表的文件描述符
 * @param fd 被注册事件的文件描述符
*/
void add_fd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET | EPOLLERR;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

/**
 * @brief 向服务器写入len字节的数据
 * @param sockfd socket文件描述符
 * @param buffer 存放要写入的数据的缓冲区
 * @param len 写入数据的长度
 * @return 是否写入成功
*/
bool write_nbytes(int sockfd, const char * buffer, int len) {
    int bytes_write = 0;
    printf("writing out %d bytes of data to socket %d...\n", len, sockfd);
    while (true) {
        bytes_write = send(sockfd, buffer, len, 0);
        if (bytes_write == -1) {
            // handle error
            return false;
        } else if (bytes_write == 0) {
            return false;
        }
        len -= bytes_write;
        buffer += bytes_write;
        if (len <= 0) {
            return true;
        }
    }
}

/**
 * @brief 从服务器读取数据
 * @param sockfd socket文件描述符
 * @param buffer 存放要读取的数据的缓冲区
 * @param len 要读取的数据的长度
 * @return 是否读取成功
*/
bool read_once(int sockfd, char * buffer, int len) {
    int bytes_read = 0;
    memset(buffer, '\0', len);
    bytes_read = recv(sockfd, buffer, len, 0);
    if (bytes_read == -1) {
        // handle error
        return false;
    } else if (bytes_read == 0) {
        return false;
    }
    printf("read in %d bytes from socket %d with content: %s\n", bytes_read,
        sockfd, buffer);
    return true;
}

/**
 * @brief 向服务器发送num个TCP连接
 * @param epollfd 标识内核中epoll事件注册表的文件描述符
 * @param num 发起的TCP连接个数
 * @param ip ip地址
 * @param port 端口号
*/
void start_conn(int epollfd, int num, const char *ip, int port) {
    int ret = 0;
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = ntohs(port);

    for (int i=0; i<num; ++i) {
        // sleep(1);
        int sockfd = socket(PF_INET, SOCK_STREAM, 0);
        printf("create 1 sock\n");
        if (sockfd < 0) {
            continue;
        }
        if (connect(sockfd, (sockaddr *)&address, sizeof(address)) == 0) {
            printf("build connection %d\n", i);
            add_fd(epollfd, sockfd);
        }
    }
}

/**
 * @brief 关闭一个连接并从epoll内核事件注册表中删除此连接的相关事件
 * @param epollfd 标识内核中epoll事件注册表的文件描述符
 * @param sockfd 标识socket连接的文件描述符
*/
void close_conn(int epollfd, int sockfd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0);
    close(sockfd);
}

int main(int argc, char * argv[]) {
    if (argc <= 3) {
        printf("usage: %s ip_address port_number conncetion_number\n",
                basename(argv[0]));
        return 1;
    }
    
    int epollfd = epoll_create(100);
    start_conn(epollfd, atoi(argv[3]), argv[1], atoi(argv[2]));
    epoll_event events[MAX_EVENT_NUMBER];
    char buffer[BUFFER_SIZE];
    while (true) {
        int fds = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIMEOUT);
        for (int i=0; i<fds; ++i) {
            int sockfd = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                if (!read_once(sockfd, buffer, BUFFER_SIZE)) {
                    close_conn(epollfd, sockfd);
                }
                struct epoll_event event;
                event.events = EPOLLOUT | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
            } else if (events[i].events & EPOLLOUT) {
                if (!write_nbytes(sockfd, request, strlen(request))) {
                    close_conn(epollfd, sockfd);
                }
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
            } else if (events[i].events & EPOLLERR) {
                close_conn(epollfd, sockfd);
            }
        }
    }
}