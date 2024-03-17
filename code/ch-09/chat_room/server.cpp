/**
 * @file server.cpp
 * @author
 * @date 2024-03-10
 * @brief 聊天室服务端程序。
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

#define BUFFER_SIZE 64  // 读缓冲区的大小
#define USER_LIMIT 5    // 最大用户数量
#define FD_LIMIT 65535  // 文件描述符数量限制

struct client_data {
    sockaddr_in address;
    char * write_buf;
    char buf[BUFFER_SIZE];
};

int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
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
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listen_fd, 5);
    assert(ret != -1);

    client_data * users = new client_data[FD_LIMIT];
    pollfd fds[USER_LIMIT+1];
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;
    for (int i=1; i<=USER_LIMIT; ++i) {
        fds[i].fd = -1;
        fds[i].events = 0;
    }
    int user_counter = 0;

    while (true) {
        ret = poll(fds, user_counter+1, -1);
        if (ret < 0) {
            printf("poll failure\n");
            break;
        }

        for (int i=0; i<=user_counter; ++i) {
            if ( (fds[i].fd == listen_fd) && (fds[i].revents & POLLIN) ) {
                sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(listen_fd, (sockaddr *)&client_address,
                                &client_addr_len);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                
                if (user_counter >= USER_LIMIT) {
                    const char * info = "too many users\n";
                    printf("%s\n", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }

                user_counter++;
                users[connfd].address = client_address;
                set_nonblocking(connfd);
                fds[user_counter].fd = connfd;
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf("comes a new user, now have %d users\n", user_counter);
            } else if ( fds[i].revents & POLLERR ) {
                printf("get an error from %d\n", fds[i].fd);
                char errors[100];
                memset(errors, '\0', 100);
                socklen_t length = sizeof(errors);
                if (getsockopt(fds[i].fd, SOL_SOCKET, SO_ERROR, errors, &length)
                 < 0) {
                    printf("get socket option failed\n");
                }
                continue;
            } else if ( fds[i].revents & POLLRDHUP ) {
                // 如果客户端关闭连接，则服务器也关闭对应的连接，并将用户数量减1
                users[fds[i].fd] = users[fds[user_counter].fd];
                close(fds[i].fd);
                fds[i] = fds[user_counter];
                --i;
                --user_counter;
                printf("a client left\n");
            } else if ( fds[i].revents & POLLIN ) {
                int connfd = fds[i].fd;
                memset(users[connfd].buf, '\0', BUFFER_SIZE);
                ret = recv(connfd, users[connfd].buf, BUFFER_SIZE-1, 0);
                printf("get %d bytes of client data %s from %d\n", ret, 
                        users[connfd].buf, connfd);
                if (ret < 0) {
                    // 如果读操作出错，则关闭连接
                    if (errno != EAGAIN) {
                        close(connfd);
                        users[connfd] = users[fds[user_counter].fd];
                        fds[i] = fds[user_counter];
                        --i;
                        --user_counter;
                    }
                } else if (ret == 0) {

                } else {
                    // 如果接收到客户端数据，则通知其他socket连接准备写数据
                    for (int j=1; j<=user_counter; ++j) {
                        if (fds[j].fd == connfd) continue;
                        fds[j].events |= ~POLLIN;
                        fds[j].events |= POLLOUT;
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            } else if ( fds[i].revents & POLLOUT ) {
                int connfd = fds[i].fd;
                if (!users[connfd].write_buf) continue;
                ret = send(connfd, users[connfd].write_buf,
                            strlen(users[connfd].write_buf), 0);
                users[connfd].write_buf = nullptr;
                fds[i].events |= ~POLLOUT;
                fds[i].events |= POLLIN;
            }
        }
    }

    delete [] users;
    close(listen_fd);
    return 0;
}