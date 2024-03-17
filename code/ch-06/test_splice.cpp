/**
 * @file test_splice.cpp
 * @author
 * @date 2024-03-08
 * @brief 用 splice 函数实现的回射服务器
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cerrno>
#include <cstring>

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

    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int conn_fd = accept(sock, (sockaddr *)&client, &client_addr_len);
    if (conn_fd < 0) {
        printf("errno is: %d\n", errno);
    } else {
        int pipefd[2];
        ret = pipe(pipefd);  // 创建管道
        assert(ret != -1);
        // 将conn_fd上流入的客户端数据定向到管道中
        ret = splice(conn_fd, nullptr, pipefd[1], nullptr, 32768, SPLICE_F_MOVE | SPLICE_F_MORE);
        assert(ret != -1);
        // 将管道的输出定向到connfd客户端连接文件描述符
        ret = splice(pipefd[0], nullptr, conn_fd, nullptr, 32768, SPLICE_F_MOVE | SPLICE_F_MORE);
        assert(ret != -1);
        close(conn_fd);
        close(pipefd[0]);
        close(pipefd[1]);
    }

    close(sock);
    return 0;
}