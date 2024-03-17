/**
 * @file pass_fd_between_processes.cpp
 * @author 
 * @date 2024-03-15
 * @brief 在进程之间传递文件描述符。
*/
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>

static const int CONTROL_LEN = CMSG_LEN(sizeof(int));

/**
 * @brief 发送文件描述符。
 * @param fd 用来传递信息的UNIX域socket。
 * @param fd_to_send 待发送的文件描述符。
 * @return
 * @exception
*/
void send_fd(int fd, int fd_to_send) {
    struct iovec iov[1];
    struct msghdr msg;
    char buf[0];

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    cmsghdr cm;
    cm.cmsg_len = CONTROL_LEN;
    cm.cmsg_level = SOL_SOCKET;
    cm.cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(&cm) = fd_to_send;
    msg.msg_control = &cm;  // 设置辅助数据
    msg.msg_controllen = CONTROL_LEN;

    sendmsg(fd, &msg, 0);
}

/**
 * @brief 接收文件描述符。
 * @param fd 用来传递信息的UNIX域socket。
 * @return 接收到的文件描述符。
 * @exception
*/
int recv_fd(int fd) {
    struct iovec iov[1];
    struct msghdr msg;
    char buf[0];

    iov[0].iov_base = buf;
    iov[0].iov_len = 1;
    msg.msg_name = nullptr;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    cmsghdr cm;
    msg.msg_control = &cm;
    msg.msg_controllen = CONTROL_LEN;

    recvmsg(fd, &msg, 0);

    int fd_to_read = *(int *)CMSG_DATA(&cm);
    return fd_to_read;
}

int main() {
    int pipefd[2];
    int fd_to_pass = 0;
    // 创建用于父子进程通信的管道
    int ret = socketpair(PF_UNIX, SOCK_DGRAM, 0, pipefd);
    assert(ret != -1);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(pipefd[0]);
        fd_to_pass = open("test.txt", O_RDWR, 0666);
        // 子进程将打开的文件描述符通过管道发送给父进程
        // 如果文件 test.txt 打开失败，子进程则将标准输入文件描述符发送给父进程
        send_fd(pipefd[1], (fd_to_pass>0)?fd_to_pass:0);
        exit(0);
    }

    close(pipefd[1]);
    // 父进程从管道接收目标文件描述符
    fd_to_pass = recv_fd(pipefd[0]);
    char buf[1024];
    memset(buf, '\0', sizeof(buf));
    // 读目标文件描述符，验证其有效性
    read(fd_to_pass, buf, sizeof(buf));
    printf("I got:\nfd: %d\ndata: %s\n", fd_to_pass, buf);
    close(fd_to_pass);
}