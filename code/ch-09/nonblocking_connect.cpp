/**
 * @file nonblocking_connect.cpp
 * @author
 * @date 2024-03-10
 * @brief 非阻塞 connect。
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#define BUFFER_SIZE 1023

int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

int unblock_connect(const char * ip, int port, int time) {
    int ret = 0;

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    int fd_opt = set_nonblocking(sockfd);
    ret = connect(sockfd, (sockaddr *)&address, sizeof(address));
    if (ret == 0) {
        // 连接成功，则恢复sockfd的属性，并立即返回
        printf("Connect to server immediately\n");
        fcntl(sockfd, F_SETFL, fd_opt);
        return sockfd;
    } else if (errno != EINPROGRESS) {
        // 如果连接还没有立即建立，那么只有当errno是EINPROGRESS时才表示连接还在进行中
        // 否则出错返回
        printf("Unblock connect not support\n");
        return -1;
    }

    fd_set read_fds;
    fd_set write_fds;
    timeval timeout;

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &write_fds);

    timeout.tv_sec = time;
    timeout.tv_usec = 0;

    ret = select(sockfd+1, nullptr, &write_fds, nullptr, &timeout);
    if (ret <= 0) {
        // select出错或超时
        printf("connection time out\n");
        close(sockfd);
        return -1;
    }

    if (!FD_ISSET(sockfd, &write_fds)) {
        printf("No events on sockfd found\n");
        close(sockfd);
        return -1;
    }

    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
        printf("get socket option failed\n");
        close(sockfd);
        return -1;
    }

    if (error != 0) {
        printf("connection failed after select with the error: %d\n", error);
        close(sockfd);
        return -1;
    }

    printf("connection ready after select with the socket: %d\n", sockfd);
    fcntl(sockfd, F_SETFL, fd_opt);
    return sockfd;
}

int main(int argc, char * argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = unblock_connect(ip, port, 10);
    if (sockfd < 0) return 1;
    close(sockfd);
    return 0;
}