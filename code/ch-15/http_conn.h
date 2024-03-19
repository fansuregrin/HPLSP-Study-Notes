/**
 * @file http_conn.h
 * @author 
 * @date 2024-03-17
 * @brief 处理HTTP连接的逻辑类的头文件
*/
#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <cstring>
#include "../ch-14/locker.h"

/**
 * @brief 
*/
class http_conn {
public:
    // 文件名最大长度
    static const int FILENAME_LEN = 512;
    // 读缓冲区的大小
    static const int READ_BUFFER_SIZE = 2048;
    // 写缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024;
    // HTTP 请求方法
    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE, TRACE,
        OPTIONS, CONNECT, PATCH
    };
    // 解析客户请求时，主状态机所处的状态
    enum CHECK_STATE {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEAD, CHECK_STATE_CONTENT
    };
    // 服务器处理HTTP请求的结果
    enum HTTP_CODE {
        NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST,
        FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
    };
    // 行的读取状态
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };
public:
    // 标识 epoll 内核事件表的文件描述符
    static int m_epollfd;
    // 统计用户数量
    static int m_user_count;
private:
    // 该http连接的socket
    int m_sockfd;
    // 客户端的 socket 地址
    sockaddr_in m_address;

    /* 读缓冲区 */
    char m_read_buf[READ_BUFFER_SIZE];
    // 读缓冲区已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    // 当前正在分析的字符在读缓冲区中的位置
    int m_checked_idx;
    // 当前正在解析的行的起始位置
    int m_start_line;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓冲区中待发送的字节数
    int m_write_idx;

    // 主状态机当前所处的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    /* 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url */ 
    char m_real_file[FILENAME_LEN];
    // 客户请求的目标文件的文件名
    char * m_url;
    // HTTP协议版本号
    char * m_version;
    // 主机名
    char * m_host;
    // HTTP 请求的消息体的长度
    int m_content_length;
    // HTTP 请求是否要求保持连接
    bool m_linger;

    // 客户请求的目标文件被 mmap 到内存中的起始位置
    char * m_file_address;
    // 目标文件的状态
    struct stat m_file_stat;
    struct iovec m_iv[2];
    // 被写内存块的数量
    int m_iv_count;
public:
    http_conn() {}
    ~http_conn() {}
public:
    void init(int sockfd, const sockaddr_in &addr);
    void close_conn(bool real_close = true);
    void process();
    bool read();
    bool write();
private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);

    // 这组函数被 process_read 调用以分析 HTTP 请求

    HTTP_CODE parse_request_line(char * text);
    HTTP_CODE parse_headers(char * text);
    HTTP_CODE parse_content(char * text);
    HTTP_CODE do_request();
    LINE_STATUS parse_line();
    char * get_line() { return m_read_buf + m_start_line; }

    // 这组函数被 process_write 调用以填充 HTTP 应答

    void unmap();
    bool add_response(const char * format, ...);
    bool add_content(const char * content);
    bool add_status_line(int status, const char * title);
    bool add_headers(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
};

#endif