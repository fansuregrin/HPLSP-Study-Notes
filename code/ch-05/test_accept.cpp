/**
 * @file test_accept.cpp
 * @author
 * @date 2024-03-06
 * @brief 接受一个异常的连接
*/
#include <signal.h>
#include <cstdio>
#include <libgen.h>
#include <cstdlib>
#include <sys/socket.h>
#include <cassert>
#include <netinet/in.h>
#include <cstring>
#include <arpa/inet.h>
#include <cerrno>

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
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    // 暂停20秒，从而等待客户端进行连接以及其他操作（如掉线或关闭等）
    sleep(20);
    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int conn_fd = accept(sock, (sockaddr *)&client, &client_addr_len);
    if (conn_fd < 0) {
        printf("errno is : %d\n", errno);
    } else {
        char remote[INET_ADDRSTRLEN];
        printf("connected with ip: %s and port: %d\n",
                inet_ntop(AF_INET, &client.sin_addr, remote, INET_ADDRSTRLEN),
                ntohs(client.sin_port));
        close(conn_fd);
    }

    close(sock);
    return 0;
}