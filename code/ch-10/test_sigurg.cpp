/**
 * @file test_sigurg.cpp
 * @author
 * @date 2024-03-11
 * @brief 使用SIGURG检测带外数据是否到达。
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cerrno>

#define BUFFER_SIZE 1024

static int connfd;

// SIGURG信号的处理函数
void sig_urg(int sig) {
    int save_errno = errno;
    char buff[BUFFER_SIZE];
    memset(buff, '\0', BUFFER_SIZE);
    int ret = recv(connfd, buff, BUFFER_SIZE-1, MSG_OOB);
    printf("got %d bytes of oob data: %s\n", ret, buff);
    errno = save_errno;
}

void add_sig(int sig, void (*sig_handler)(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
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
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);
    
    ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);
    
    ret = listen(listen_fd, 5);
    assert(ret != -1);

    sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    connfd = accept(listen_fd, (sockaddr *)&client_address, &client_addr_len);
    if (connfd < 0) {
        printf("errno is: %d\n", errno);
    } else {
        add_sig(SIGURG, sig_urg);
        // 使用SIGURG信号之前，必须设置socket的宿主进程或进程组
        fcntl(connfd, F_SETOWN, getpid());
        
        char buffer[BUFFER_SIZE];
        while (true) {  // 循环接收普通数据
            memset(buffer, '\0', BUFFER_SIZE);
            ret = recv(connfd, buffer, BUFFER_SIZE-1, 0);
            if (ret <= 0) break;
            printf("got %d bytes of normal data: %s\n", ret, buffer);
        }
        close(connfd);
    }

    close(listen_fd);
    return 0;
}