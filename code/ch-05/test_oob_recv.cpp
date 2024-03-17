/**
 * @file test_oob_recv.cpp
 * @author
 * @date 2024-03-06
 * @brief 接收带外数据
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

#define BUF_SIZE 100

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

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

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
        ret = recv(conn_fd, buffer, BUF_SIZE-1, 0);
        printf("got %d byte(s) of normal data [%s]\n", ret, buffer);
        
        memset(buffer, '\0', BUF_SIZE);
        ret = recv(conn_fd, buffer, BUF_SIZE-1, MSG_OOB);
        printf("got %d byte(s) of oob data [%s]\n", ret, buffer);

        memset(buffer, '\0', BUF_SIZE);
        ret = recv(conn_fd, buffer, BUF_SIZE-1, 0);
        printf("got %d byte(s) of normal data [%s]\n", ret, buffer);

        close(conn_fd);
    }

    close(sock);
    return 0;
}