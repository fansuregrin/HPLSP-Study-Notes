/**
 * @file set_send_buffer.cpp
 * @author
 * @date 2024-03-07
 * @brief 修改TCP发送缓冲区的服务器程序
*/
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <unistd.h>

#define BUF_SIZE 512

int main(int argc, char * argv[]) {
    if (argc <= 3) {
        printf("usage: %s ip_address port_number send_buffer_size\n",
                basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);
    
    sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_address.sin_addr);
    server_address.sin_port = htons(port);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int send_buf = atoi(argv[3]);
    int len = sizeof(send_buf);
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf, sizeof(send_buf));
    getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_buf, (socklen_t *)&len);
    printf("the tcp send buffer size after setting is %d\n", send_buf);

    if (connect(sock, (sockaddr *)&server_address, sizeof(server_address))
        != -1) {
        char buffer[BUF_SIZE];
        memset(buffer, 'a', BUF_SIZE);
        send(sock, buffer, BUF_SIZE, 0);
    }

    close(sock);
    return 0;
}
