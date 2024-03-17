/**
 * @file processpool.h
 * @author
 * @date 2024-03-16
 * @brief 半同步/半异步进程池
*/
#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>

/**
 * @brief 描述一个子进程的类
*/
class process {
public:
    process(): m_pid(-1) {}
public:
    pid_t m_pid;      // 子进程pid
    int m_pipefd[2];  // 父子进程通信用的管道
};

/**
 * @brief 进程池类模板
*/
template<typename T>
class processpool {
private:
    processpool(int listenfd, int process_number=0);
public:
    /**
     * @brief 创建一个 processpool<T> 实例
     * @param listenfd 监听 socket 文件描述符
     * @param process_number 进程数量
     * @return processpool<T> 指针
    */
    static processpool<T>* create(int listenfd, int process_number=2) {
        if (!m_instance) {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }

    ~processpool() {
        delete [] m_sub_process;
    }

    void run();
private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();
private:
    // 进程池允许的最大子进程数量
    static const int MAX_PROCESS_NUMBER = 16;
    // 每个子进程最多能处理的客户数量
    static const int USER_PER_PROCESS = 65536;
    // epoll 最多能处理的事件数
    static const int MAX_EVENT_NUMBER = 10000;
    // 进程池中的进程总数
    int m_process_number;
    // 子进程在进程池中的序号（0-index）
    int m_idx;
    // 进程的 epoll 内核时间表
    int m_epollfd;
    // 监听 socket
    int m_listenfd;
    // 进程通过 m_stop 来决定是否停止运行
    int m_stop;
    // 保存所有子进程的描述信息
    process * m_sub_process;
    // 进程池静态实例
    static processpool<T> * m_instance;
};

template<typename T>
processpool<T>* processpool<T>::m_instance = nullptr;

// 用于处理信号的管道，以实现统一事件源。后面称为“信号管道”。
static int sig_pipefd[2];

/**
 * @brief 将指定文件描述符设为非阻塞的
 * @param fd 文件描述符
 * @return 原来的文件状态标志
*/
static int set_nonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * @brief 为指定的文件描述符注册事件
 * @param epollfd 标识 epoll 内核事件表的文件描述符
 * @param fd 被注册事件的文件描述符
*/
static void add_fd(int epollfd, int fd) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

/**
 * @brief 从 epollfd 标识的 epoll 内核事件表中删除 fd 上的所有注册事件
 * @param epollfd 标识 epoll 内核事件表的文件描述符
 * @param fd 被注册事件的文件描述符
*/
static void remove_fd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

/**
 * @brief 信号处理器：通过信号管道传递信号
 * @param sig 信号值
*/
static void sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

/**
 * @brief 添加信号
 * @param sig 要添加的信号
 * @param handler 信号处理器函数
 * @param restart 被信号中断后是否自动重启
*/
static void add_sig(int sig, void(*handler)(int), bool restart=true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, nullptr) != -1);
}


/**
 * @brief 私有构造函数
 * @param listenfd 监听 socket 文件描述符
 * @param process_number 进程数量
*/
template<typename T>
processpool<T>::processpool(int listenfd, int process_number)
: m_listenfd(listenfd), m_process_number(process_number), 
  m_idx(-1), m_stop(false) {
    assert((process_number>0) && (process_number<=MAX_PROCESS_NUMBER));

    m_sub_process = new process[process_number];
    assert(m_sub_process);

    // 创建 process_number 个子进程，并创建它们和父进程之间的管道
    for (int i=0; i<process_number; ++i) {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(ret == 0);

        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0) {
            close(m_sub_process[i].m_pipefd[1]);
            continue;
        } else {
            close(m_sub_process[i].m_pipefd[0]);
            m_idx = i;
            break;
        }
    }
}

/**
 * @brief 统一事件源
*/
template<typename T>
void processpool<T>::setup_sig_pipe() {
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    set_nonblocking(sig_pipefd[1]);
    add_fd(m_epollfd, sig_pipefd[0]);

    add_sig(SIGCHLD, sig_handler);
    add_sig(SIGTERM, sig_handler);
    add_sig(SIGINT, sig_handler);
    add_sig(SIGPIPE, SIG_IGN);
}

/**
 * @brief 启动进程池
*/
template<typename T>
void processpool<T>::run() {
    if (m_idx != -1) {
        run_child();
        return;
    }
    run_parent();
}

template<typename T>
void processpool<T>::run_child() {
    setup_sig_pipe();

    // 每个子进程通过其在进程池中的序号 m_idx 找到与父进程通信的管道
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    // 子进程需要监听管道文件描述符 pipefd, 因为父进程将通过管道来通知子进程 accept 新连接
    add_fd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    T* users = new T[USER_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;

    while (!m_stop) {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            if ((sockfd == pipefd) && (events[i].events & EPOLLIN)) {
                int client = 0;
                // 从父子进程之间的管道读取数据，并将结果保存在变量 client 中。
                // 如果读取成功，则表示有新客户连接到来。
                ret = recv(pipefd, (char *)&client, sizeof(client), 0);
                if ((ret < 0 && errno != EAGAIN) || ret == 0) {
                    continue;
                }
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(m_listenfd, (sockaddr *)&client_addr,
                                &client_addr_len);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                add_fd(m_epollfd, connfd);
                users[connfd].init(m_epollfd, connfd, client_addr);
            }
            // 处理信号
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                }
                for (int i=0; i<ret; ++i) {
                    switch (signals[i]) {
                        case SIGCHLD: {
                            pid_t pid;
                            int stat;
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                                continue;
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT: {
                            m_stop = true;
                            break;
                        }
                    }
                }
            }
            // 客户请求到来。调用逻辑处理对象的 process 方法处理。
            else if (events[i].events & EPOLLIN) {
                users[sockfd].process();
            }
            else {
                continue;
            }
        }
    }

    delete [] users;
    users = nullptr;
    close(pipefd);
    close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent() {
    setup_sig_pipe();

    // 父进程监听 m_listenfd
    add_fd(m_epollfd, m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while (!m_stop) {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR) {
            printf("epoll failure\n");
            break;
        }

        for (int i=0; i<number; ++i) {
            int sockfd = events[i].data.fd;
            if (sockfd == m_listenfd) {
                // 如果有新连接到来，就采用 Round Robin 方式将其分配给一个子进程处理
                int i = sub_process_counter;
                do {
                    if (m_sub_process[i].m_pid != -1) {
                        break;
                    }
                    i = (i+1)%m_process_number;
                } while (i != sub_process_counter);
                if (m_sub_process[i].m_pid == -1) {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;

                send(m_sub_process[i].m_pipefd[0], (char *)&new_conn,
                    sizeof(new_conn), 0);
                printf("send request to child [%d]\n", i);
            }
            // 处理信号
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN)) {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0) {
                    continue;
                }
                for (int i=0; i<ret; ++i) {
                    switch (signals[i]) {
                        case SIGCHLD: {
                            pid_t pid;
                            int stat;
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                                for (int i=0; i<m_process_number; ++i) {
                                    if (pid == m_sub_process[i].m_pid) {
                                        printf("child [%d] join\n", i);
                                        close(m_sub_process[i].m_pipefd[0]);
                                        m_sub_process[i].m_pid = -1;
                                    }
                                }
                            }
                            m_stop = true;
                            for (int i=0; i<m_process_number; ++i) {
                                if (m_sub_process[i].m_pid != -1) {
                                    m_stop = false;
                                } 
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT: {
                            printf("kill all child now\n");
                            for (int i=0; i<m_process_number; ++i) {
                                int pid = m_sub_process[i].m_pid;
                                if (pid != -1) {
                                    kill(pid, SIGTERM);
                                }
                            }
                            break;
                        }
                    }
                }
            }
            else {
                continue;
            }
        }
    }

    close(m_epollfd);
}

#endif