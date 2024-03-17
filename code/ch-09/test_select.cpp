/**
 * @file test_select.cpp
 * @author
 * @date 2024-03-09
 * @brief 同时接收普通数据和带外数据。
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
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

    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int conn_fd = accept(sock, (sockaddr *)&client, &client_addr_len);
    if (conn_fd < 0) {
        printf("errno is: %d\n", errno);
    }

    char buffer[1024];
    fd_set read_fds;
    fd_set exception_fds;
    FD_ZERO(&read_fds);
    FD_ZERO(&exception_fds);

    while (true) {
        FD_SET(conn_fd, &read_fds);
        FD_SET(conn_fd, &exception_fds);
        ret = select(conn_fd+1, &read_fds, nullptr, &exception_fds, nullptr);
        if (ret < 0) {
            printf("selection failure\n");
            break;
        }
        if (FD_ISSET(conn_fd, &read_fds)) {
            // 对于可读事件，采用普通的recv函数读取数据
            memset(buffer, '\0', sizeof(buffer));
            ret = recv(conn_fd, buffer, sizeof(buffer)-1, 0);
            if (ret <= 0) break;
            printf("get %d byte(s) of normal data: %s\n", ret, buffer);
        } 
        if (FD_ISSET(conn_fd, &exception_fds)) {
            memset(buffer, '\0', sizeof(buffer));
            // 对于异常事件，采用带MSG_OOB标志的recv函数读取带外数据
            ret = recv(conn_fd, buffer, sizeof(buffer)-1, MSG_OOB);
            if (ret <= 0) break;
            printf("get %d byte(s) of oob data: %s\n", ret, buffer);
        }
    }

    close(conn_fd);
    close(sock);
    return 0;
}