/**
 * @file client.cpp
 * @author
 * @date 2024-03-10
 * @brief 聊天室客户端程序。
*/
#define _GNU_SOURCE 1
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#define BUFFER_SIZE 64

int main(int argc, char * argv[]) {
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_address.sin_addr);
    server_address.sin_port = htons(port);

    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sockfd >= 0);

    if ( connect(sockfd, (sockaddr *)&server_address, sizeof(server_address))
    < 0 ) {
        printf("connection failed\n");
        close(sockfd);
        return 1;
    }

    pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = sockfd;
    fds[1].events = POLLIN | POLLRDHUP;
    fds[1].revents = 0;

    char read_buf[BUFFER_SIZE];
    int pipefd[2];
    int ret = pipe(pipefd);
    assert(ret != -1);

    while (true) {
        ret = poll(fds, 2, -1);
        if (ret < 0) {
            printf("poll failure\n");
            break;
        }

        if (fds[1].revents & POLLRDHUP) {
            printf("server closed the connection\n");
            break;
        } else if (fds[1].revents & POLLIN) {
            memset(read_buf, '\0', BUFFER_SIZE);
            recv(fds[1].fd, read_buf, BUFFER_SIZE-1, 0);
            printf("%s\n", read_buf);
        }

        if (fds[0].revents & POLLIN) {
            ret = splice(STDIN_FILENO, nullptr, pipefd[1], nullptr, 32768,
                    SPLICE_F_MORE | SPLICE_F_MOVE);
            ret = splice(pipefd[0], nullptr, sockfd, nullptr, 32768,
                    SPLICE_F_MORE | SPLICE_F_MOVE);
        }
    }

    close(sockfd);
    return 0;
}