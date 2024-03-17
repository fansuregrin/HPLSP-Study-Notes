/**
 * @file chatroom_server.cpp
 * @author
 * @date 2024-03-14
 * @brief 使用共享内存的聊天室服务器程序
*/
#include <sys/socket.h> // socket, setsockopt, connect, send
#include <netinet/in.h> // sockaddr_in, htons
#include <arpa/inet.h>  // inet_pton
#include <sys/epoll.h>  // epoll_event, epoll_ctl, epoll_wait, epoll_create
#include <signal.h> // sigaction, sigfillset
#include <sys/mman.h>  // shm_unlink
#include <sys/wait.h>  // waitpid
#include <fcntl.h>  // fcntl
#include <unistd.h> // close, ftruncate
#include <cstring>  // basename, bzero
#include <cstdio>   // printf
#include <cstdlib>  // atoi
#include <cassert>  // assert
#include <cerrno>   // errno

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define PROCESS_LIMIT 4194304

// 处理一个客户连接必要的数据
struct client_data {
    sockaddr_in address;    // 客户端的 socket 地址
    int connfd;             // socket 文件描述符
    pid_t pid;              // 处理这个连接的子进程的pid
    int pipefd[2];          // 和父进程通信用的管道
};

static const char * shm_name = "/my_shm";
int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char * share_mem = nullptr;
// 客户连接数组。进程用客户连接的编号来索引这个数组，即可获得相关的客户连接数据。
client_data * users = nullptr;
// 子进程和客户连接的映射关系表。用进程的pid来索引这个数组，即可获得该进程所处理的
// 客户连接的编号。
int * sub_process = nullptr;
// 当前客户数量
int user_count = 0;
bool stop_child = false;

int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void add_fd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void add_sig(int sig, void (*handler)(int), bool restart=true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void del_resourse() {
    close(sig_pipefd[0]);
    close(sig_pipefd[1]);
    close(listenfd);
    close(epollfd);
    shm_unlink(shm_name);
    delete [] users;
    delete [] sub_process;
}

void child_term_handler(int sig) {
    stop_child = true;
}

/* 子进程运行函数
idx: 该子进程处理的客户连接编号
users: 保存所有客户连接数据的数组
share_mem: 共享内存的起始地址
*/
int run_child(int idx, client_data *users, char *share_mem) {
    epoll_event events[MAX_EVENT_NUMBER];
    int child_epollfd = epoll_create(5);
    assert(child_epollfd != -1);
    
    // 子进程使用I/O复用技术来同时监听两个文件描述符：
    //   （1）客户连接 socket
    //   （2）与父进程通信的管道文件描述符
    int connfd = users[idx].connfd;
    add_fd(child_epollfd, connfd);
    int pipefd = users[idx].pipefd[1];
    add_fd(child_epollfd, pipefd);

    add_sig(SIGTERM, child_term_handler, false);

    int ret;
    while (!stop_child) {
        int number = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            // 本子进程负责的客户连接有数据到达
            if ((sockfd == connfd) && (events[i].events & EPOLLIN)) {
                memset(share_mem+idx*BUFFER_SIZE, '\0', BUFFER_SIZE);
                ret = recv(connfd, share_mem+idx*BUFFER_SIZE, BUFFER_SIZE-1, 0);
                if (ret < 0) {
                    if (errno != EAGAIN) {
                        stop_child = true;
                    }
                } else if (ret == 0) {
                    stop_child = true;
                } else {
                    // 成功读取客户数据后就通知主进程（通过管道）来处理
                    send(pipefd, (char *)&idx, sizeof(idx), 0);
                }
            }
            // 主进程通知本进程（通过管道）将第client个客户的数据发送到本进程负责的
            // 客户端
            else if ((sockfd == pipefd) && (events[i].events & EPOLLIN)) {
                int client = 0;
                // 接收主进程发来的数据，即有客户数据到达的连接编号
                ret = recv(pipefd, (char *)&client, sizeof(client), 0);
                if (ret < 0) {
                    if (errno != EAGAIN) {
                        stop_child = true;
                    }
                } else if (ret == 0) {
                    stop_child = true;
                } else {
                    send(connfd, share_mem+client*BUFFER_SIZE, BUFFER_SIZE, 0);
                }
            } else {
                // others
                continue;
            }
        }
    }

    close(connfd);
    close(pipefd);
    close(child_epollfd);
    return 0;
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

    user_count = 0;
    users = new client_data[USER_LIMIT+1];
    sub_process = new int[PROCESS_LIMIT];
    for (int i=0; i<PROCESS_LIMIT; ++i) {
        sub_process[i] = -1;
    }

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    add_fd(epollfd, listen_fd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    set_nonblocking(sig_pipefd[1]);
    add_fd(epollfd, sig_pipefd[0]);

    add_sig(SIGCHLD, sig_handler);
    add_sig(SIGTERM, sig_handler);
    add_sig(SIGINT, sig_handler);
    add_sig(SIGPIPE, SIG_IGN);  // ???
    bool stop_server = false;
    bool terminate = false;

    // 创建共享内存，作为所有客户 socket 连接的读缓存
    shmfd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    assert(shmfd != -1);
    ret = ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE);
    assert(ret != -1);
    share_mem = (char *)mmap(nullptr, USER_LIMIT*BUFFER_SIZE,
                PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    assert(share_mem != MAP_FAILED);
    close(shmfd);

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            // 新的客户连接到来
            if (sockfd == listen_fd) {
                struct sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(listen_fd, (sockaddr *)&client_address,
                                &client_addr_len);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (user_count >= USER_LIMIT) {
                    const char * info = "too many users\n";
                    printf("%s", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }
                // 保存第 user_count 个客户连接的相关数据
                users[user_count].address = client_address;
                users[user_count].connfd = connfd;
                // 在主进程和子进程之间建立管道，以传递必要的数据
                ret = socketpair(PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd);
                assert(ret != -1);
                pid_t pid = fork();
                if (pid < 0) {
                    close(connfd);
                    continue;
                } else if (pid == 0) {
                    close(epollfd);
                    close(listen_fd);
                    close(users[user_count].pipefd[0]);  // ???
                    close(sig_pipefd[0]);
                    close(sig_pipefd[1]);
                    run_child(user_count, users, share_mem);
                    munmap((void *)share_mem, USER_LIMIT*BUFFER_SIZE);
                    exit(0);
                } else {
                    close(connfd);
                    close(users[user_count].pipefd[1]);  // ???
                    add_fd(epollfd, users[user_count].pipefd[0]);
                    users[user_count].pid = pid;
                    sub_process[pid] = user_count;
                    ++user_count;
                }
            }
            // 处理信号事件
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    // handle error
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i=0; i<ret; ++i) {
                        switch (signals[i]) {
                            // 子进程退出，表示有某个客户端关闭了连接
                            case SIGCHLD: {
                                pid_t pid;
                                int stat;
                                while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                                    // 用子进程的 pid 获取被关闭的客户连接的编号
                                    int del_user = sub_process[pid];
                                    sub_process[pid] = -1;
                                    if (del_user<0 || del_user>USER_LIMIT) {
                                        continue;
                                    }
                                    // 清除第 del_user 个客户连接使用的相关数据
                                    epoll_ctl(epollfd, EPOLL_CTL_DEL,
                                            users[del_user].pipefd[0], 0);
                                    close(users[del_user].pipefd[0]);
                                    users[del_user] = users[--user_count];
                                    sub_process[users[del_user].pid] = del_user;
                                }
                                if (terminate && user_count==0) {
                                    stop_server = true;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT: {
                                // 结束服务器程序
                                printf("kill all the child now\n");
                                if (user_count == 0) {
                                    stop_server = true;
                                    break;
                                }
                                for (int i=0; i<user_count; ++i) {
                                    int pid = users[i].pid;
                                    kill(pid, SIGTERM);
                                }
                                terminate = true;
                                break;
                            }
                            default: break;
                        }
                    }
                }
            }
            // 某个子进程向父进程写入了数据
            else if (events[i].events & EPOLLIN) {
                int child = 0;
                // 读取管道数据，child 变量记录了是哪个客户连接有数据到达
                ret = recv(sockfd, (char *)&child, sizeof(child), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    // 向除了负责第 child 个客户连接的子进程之外的其他子进程发送
                    // 消息，通知它们有客户数据要写
                    for (int j=0; j<user_count; ++j) {
                        if (users[j].pipefd[0] != sockfd) {
                            printf("send data to child across pipe\n");
                            send(users[j].pipefd[0], (char *)&child,
                                sizeof(child), 0);
                        }
                    }
                }
            }
        }
    }

    del_resourse();
    return 0;
}