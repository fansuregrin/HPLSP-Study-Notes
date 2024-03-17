#include <sys/types.h>
#include <sys/socket.h> // socket, setsockopt, connect, send
#include <netinet/in.h> // sockaddr_in, htons
#include <arpa/inet.h>  // inet_pton
#include <sys/epoll.h>  // epoll_event, epoll_ctl, epoll_wait, epoll_create
#include <signal.h> // sigaction, sigfillset
#include <fcntl.h>  // fcntl
#include <unistd.h> // close, alarm
#include <cstring>  // basename, bzero
#include <cstdio>   // printf
#include <cstdlib>  // atoi
#include <cassert>  // assert
#include <cerrno>   // errno
#include "time_wheel_timer.h"

#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define TIMESLOT 1

static int pipefd[2];
static time_wheel tw;
static int epollfd = 0;

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
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void add_sig(int sig) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = sig_handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}

void timer_handler() {
    // 定时处理任务，实际上是调用 tick 函数
    tw.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，它删除非活动连接 socket 上的注册事件，并将其关闭
void cb_func(client_data * user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    printf("close fd: %d\n", user_data->sockfd);
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

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    add_fd(epollfd, listen_fd);

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    set_nonblocking(pipefd[1]);
    add_fd(epollfd, pipefd[0]);

    // 设置信号处理函数
    add_sig(SIGALRM);
    add_sig(SIGTERM);
    bool stop_sever = false;
    client_data * users = new client_data[FD_LIMIT];
    bool timeout = false;
    alarm(TIMESLOT);  // 定时

    while (!stop_sever) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            // 处理新到的客户连接
            if (sockfd == listen_fd) {
                sockaddr_in client_address;
                socklen_t client_addr_len = sizeof(client_address);
                int connfd = accept(listen_fd, (sockaddr *)&client_address, 
                                &client_addr_len);
                add_fd(epollfd, connfd);
                users[connfd].address = client_address;
                users[connfd].sockfd = connfd;
                // 创建定时器，设置其回调函数与超时时间，然后绑定定时器与用户数据，
                // 最后将定时器添加到定时器链表中
                tw_timer * timer = tw.add_timer(10*TIMESLOT);
                timer->cb_func = cb_func;
                timer->user_data = &users[connfd];
                users[connfd].timer = timer;
            }
            // 处理信号 
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                char signals[1024];
                ret = recv(sockfd, signals, sizeof(signals), 0);
                if (ret == -1) {
                    // handle error
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int i=0; i<ret; ++i) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                // 使用 timeout 标记有定时任务需要处理，但不立即处理
                                // 定时任务。因为定时任务的优先级不是很高，我们优先处理
                                // 其他重要任务。
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_sever = true;
                            }
                        }
                    }
                }
            }
            // 处理客户连接上接收到的数据
            else if (events[i].events & EPOLLIN) {
                memset(users[sockfd].buf, '\0', BUFFER_SIZE);
                ret = recv(sockfd, users[sockfd].buf, BUFFER_SIZE-1, 0);
                printf("get [%d] bytes of client data [%s] from [%d]\n",
                        ret, users[sockfd].buf, sockfd);
                
                tw_timer * timer = users[sockfd].timer;
                if (ret < 0) {
                    // 如果发生错误，则关闭连接，并移除其对应的定时器
                    if (errno != EAGAIN) {
                        cb_func(&users[sockfd]);
                        if (timer) {
                            tw.del_timer(timer);
                        }
                    }
                } else if (ret == 0) {
                    // 如果对方已经关闭连接，则我们也关闭连接，并移除对应的定时器
                    cb_func(&users[sockfd]);
                    if (timer) {
                        tw.del_timer(timer);
                    }
                } else {
                    // 如果某个客户连接上有数据可读，我们需要调整该连接对应的定时器，
                    // 以延长该连接被关闭的时间。
                    if (timer) {
                        tw.del_timer(timer);
                        printf("adjust time once\n");
                        tw_timer * new_timer = tw.add_timer(10*TIMESLOT);
                        new_timer->cb_func = cb_func;
                        new_timer->user_data = &users[sockfd];
                        users[sockfd].timer = new_timer;
                    }
                }
            }
            else {
                // others
            }
        }

        // 最后处理定时事件
        if (timeout) {
            timer_handler();
            timeout = false;
        }
    }

    close(epollfd);
    close(listen_fd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete users;

    return 0;
}