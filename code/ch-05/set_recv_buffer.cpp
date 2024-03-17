/**
 * @file set_recv_buffer.cpp
 * @author
 * @date 2024-03-07
 * @brief 修改TCP接收缓冲区的服务器程序
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cerrno>

#define BUF_SIZE 1024

int main(int argc, char * argv[]) {
    if (argc <= 3) {
        printf("usage: %s ip_address port_number recv_buffer_size\n",
                basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int recv_buf = atoi(argv[3]);
    int len = sizeof(recv_buf);
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf, len);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf, (socklen_t *)&len);
    printf("the tcp receive buffer size after setting is %d\n", recv_buf);

    int ret = bind(sock, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int conn_fd = accept(sock, (sockaddr *)&client, &client_addr_len);
    if (conn_fd < 0) {
        printf("errno is: %d\n", errno);
    } else {
        char buffer[BUF_SIZE];

        memset(buffer, '\0', BUF_SIZE);
        while (recv(conn_fd, buffer, BUF_SIZE-1, 0) > 0) {}
        close(conn_fd);
    }

    close(sock);
    return 0;
}