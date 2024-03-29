/**
 * @file http_conn.cpp
 * @author
 * @date 2024-03-17
 * @brief 
*/
#include "http_conn.h"

// HTTP响应的状态文本信息(reason-phrase/status-text)

const char * ok_200_title = "OK";
const char * error_400_title = "Bad Request";
const char * error_400_form = "Your request has bad syntax or is inherently "
                              "impossible to satisfy.\n";
const char * error_403_title = "Forbidden";
const char * error_403_form = "You do not have permission to get file from "
                              "this server.\n";
const char * error_404_title = "Not Found";
const char * error_404_form = "The requested file was not found on this server.\n";
const char * error_500_title = "Internal Error";
const char * error_500_form = "There was an unusual problem serving the "
                              "requested file.\n";

// 网站根目录
const char * doc_root = "/home/fansuregrin/test_website";

/**
 * @brief 将指定文件描述符设为非阻塞的
 * @param fd 文件描述符
 * @return 原来的文件状态标志
*/
int set_nonblocking(int fd) {
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
void add_fd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

/**
 * @brief 从 epollfd 标识的 epoll 内核事件表中删除 fd 上的所有注册事件
 * @param epollfd 标识 epoll 内核事件表的文件描述符
 * @param fd 被注册事件的文件描述符
*/
void remove_fd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

/**
 * @brief 从 epollfd 标识的 epoll 内核事件表中修改 fd 上的注册事件
 * @param epollfd 标识 epoll 内核事件表的文件描述符
 * @param fd 被注册事件的文件描述符
 * @param ev 新添加的事件
*/
void mod_fd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// ========================
// http_conn 类成员 BEGIN
// ========================

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

/**
 * @brief 初始化新接收的连接
 * @param sockfd socket 文件描述符
 * @param addr 客户端 socket 地址
*/
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    #ifdef DEBUG
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    #endif
    add_fd(m_epollfd, m_sockfd, true);
    m_user_count++;

    init();
}

/**
 * @brief 关闭连接
 * @param real_close 
*/
void http_conn::close_conn(bool real_close) {
    if (real_close && m_sockfd != -1) {
        remove_fd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

/**
 * @brief 处理HTTP请求的入口函数，由线程池中的工作线程调用
*/
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        mod_fd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    mod_fd(m_epollfd, m_sockfd, EPOLLOUT);
}

/**
 * @brief 通过连接读取客户数据
 * @return 是否读取成功
*/
bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx,
                        READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }

    return true;
}

/**
 * @brief 写 HTTP 响应
 * @return 是否写成功
*/
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        mod_fd(m_epollfd, m_sockfd, EPOLLIN);
        return true;
    }
    while (true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            if (errno == EAGAIN) {
                mod_fd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send) {
            unmap();
            if (m_linger) {
                init();
                mod_fd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            } else {
                mod_fd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

/**
 * @brief 初始化连接的私有辅助函数
*/
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;

    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/**
 * @brief 处理和分析读取到的客户数据
 * @return 服务器处理 HTTP 请求的结果
*/
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           (line_status = parse_line()) == LINE_OK ) {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEAD: {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

/**
 * @brief 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
 * @param ret 服务器处理 HTTP 请求的结果
 * @return 是否处理成功
*/
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            } else {
                const char * ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default : {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/**
 * @brief 从状态机，用于解析出一行内容
 * @return 行的状态
*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if (m_checked_idx + 1 == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx+1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx-1] == '\r') {
                m_read_buf[m_checked_idx-1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/**
 * @brief 解析 HTTP 请求行
 * @param text 一行内容
 * @return 服务器处理 HTTP 请求的结果
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char * text) {
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    // 只支持 GET 方法
    char * method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0]!='/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEAD;
    return NO_REQUEST;
}

/**
 * @brief 解析 HTTP 请求的一个头部字段
 * @param text 一行内容
 * @return 服务器处理 HTTP 请求的结果
*/
http_conn::HTTP_CODE http_conn::parse_headers(char * text) {
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        printf("oop! unknow header: %s\n", text);
    }

    return NO_REQUEST;
}

/**
 * @brief 解析 HTTP 请求的消息体。
 * (此处并没有真正地解析消息体内容，只是判断消息体是否完整读入。)
 * @param text 被解析的内容
 * @return 服务器处理 HTTP 请求的结果
 * 
*/
http_conn::HTTP_CODE http_conn::parse_content(char * text) {
    if (m_read_idx >= m_checked_idx + m_content_length) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/**
 * @brief 尝试将请求的目标文件映射到内存中
 * @return 服务器处理 HTTP 请求的结果
*/
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file+len, m_url, FILENAME_LEN-len-1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    if (!(m_file_stat.st_mode & S_IRUSR)) {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE,
                            fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/**
 * @brief 对内存映射区执行 unmap 操作
*/
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

/**
 * @brief 向写缓冲区中写入待发送的数据
 * @param format 用于格式化的字符串
 * @return 是否写入成功
*/
bool http_conn::add_response(const char * format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf+m_write_idx, WRITE_BUFFER_SIZE-1-m_write_idx,
                format, arg_list);
    if (len >= WRITE_BUFFER_SIZE-1-m_write_idx) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

/**
 * @brief 添加响应的消息体(Message-Body)
 * @param content 需要添加的消息体
 * @return 是否添加成功
*/
bool http_conn::add_content(const char * content) {
    return add_response("%s", content);
}

/**
 * @brief 添加响应的状态行(Status-Line)
 * @param status 状态码 (status-code)
 * @param title 状态文本 (status-text/reason-phrase)
 * @return 是否添加成功
*/
bool http_conn::add_status_line(int status, const char * title) {
    // Status-Line: protocol-version + status-code + status-text
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/**
 * @brief 添加响应的头部字段
 * @param content_len Content-Length字段的值
 * @return 是否添加成功
*/
bool http_conn::add_headers(int content_len) {
    return add_content_length(content_len) &&
           add_linger() &&
           add_blank_line();
}

/**
 * @brief 添加头部中的Content-Length字段
 * @param content_len Content-Length字段的值
 * @return 是否添加成功
*/
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

/**
 * @brief 添加头部中的Connection字段
 * @return 是否添加成功
*/
bool http_conn::add_linger() {
    return add_response("Connection: %s\r\n", m_linger?"keep-alive":"close");
}

/**
 * @brief 添加头部和消息体之间的空行
 * @return 是否添加成功
*/
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
// ========================
// http_conn 类成员 END
// ========================