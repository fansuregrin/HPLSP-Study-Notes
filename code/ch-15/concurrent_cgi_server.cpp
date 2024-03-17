/**
 * @file concurrent_cgi_server.cpp
 * @author
 * @date 2024-03-16
 * @brief 用进程池实现的并发 CGI 服务器
*/
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

#include "processpool.h"

/**
 * @brief 处理用户CGI请求的类
*/
class cgi_conn {
public:
    cgi_conn() {}
    ~cgi_conn() {}

    void init(int epollfd, int sockfd, sockaddr_in &client_addr) {
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buf, '\0', BUFFER_SIZE);
        m_read_idx = 0;
    }

    void process() {
        int idx = 0;
        int ret = -1;
        while (true) {
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buf+idx, BUFFER_SIZE-1-idx, 0);
            if (ret < 0) {
                if (errno != EAGAIN) {
                    remove_fd(m_epollfd, m_sockfd);
                }
                break;
            } else if (ret == 0) {
                remove_fd(m_epollfd, m_sockfd);
                break;
            } else {
                m_read_idx += ret;
                printf("user content is: %s\n", m_buf);
                // 如果遇到字符 "\r\n"，则开始处理客户请求
                for (; idx < m_read_idx; ++idx) {
                    if (idx>=1 && m_buf[idx-1]=='\r' && m_buf[idx]=='\n') {
                        break;
                    }
                }
                // 如果没有遇到字符 "\r\n"，则需要继续读取更多的客户数据
                if (idx == m_read_idx) {
                    continue;
                }
                m_buf[idx-1] = '\0';

                char * filename = m_buf;
                printf("filename is: %s\n", filename);
                // 判断客户要运行的CGI程序是否存在
                if (access(filename, F_OK) == -1) {
                    remove_fd(m_epollfd, m_sockfd);
                    break;
                }
                // 创建子进程来执行CGI程序
                ret = fork();
                if (ret == -1) {
                    remove_fd(m_epollfd, m_sockfd);
                    break;
                } else if (ret > 0) {
                    remove_fd(m_epollfd, m_sockfd);
                    break;
                } else {
                    close(STDOUT_FILENO);
                    dup(m_sockfd);
                    execl(m_buf, m_buf, 0);
                    exit(0);
                }
            }
        }
    }
private:
    static const int BUFFER_SIZE = 1024;
    static int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];
    int m_read_idx;
};

int cgi_conn::m_epollfd = -1;

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

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = bind(listenfd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
    if (pool) {
        pool->run();
        delete pool;
    }

    close(listenfd);
    return 0;
}