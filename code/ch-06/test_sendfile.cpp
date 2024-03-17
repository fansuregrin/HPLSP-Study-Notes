/**
 * @file test_sendfile.cpp
 * @author
 * @date 2024-03-08
 * @brief 用 sendfile 函数传输文件 (零拷贝)
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cerrno>
#include <cstring>

int main(int argc, char * argv[]) {
    if (argc <= 3) {
        printf("usage: %s ip_address port_number filename\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);
    const char * filename = argv[3];

    int file_fd = open(filename, O_RDONLY);
    assert(file_fd > 0);
    struct stat stat_buf;
    fstat(file_fd, &stat_buf);

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
        sendfile(conn_fd, file_fd, nullptr, stat_buf.st_size);
        close(conn_fd);
    }

    close(sock);
    return 0;
}