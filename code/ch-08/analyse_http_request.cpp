/**
 * @file analyse_http_request.cpp
 * @author
 * @date 2024-03-08
 * @brief HTTP请求的读取和分析
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cerrno>

#define BUFFER_SIZE 4096  // 读缓冲区大小

// 主状态机的两种可能状态，分别表示：
//     （1）当前正在分析请求行
//     （2）当前正在分析头部字段
enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER };

// 从状态机的三种可能状态（即行的读取状态），分别表示：
//     （1）读取到一个完整的行
//     （2）行出错
//     （3）行数据还不完整
enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

// 服务器处理http请求的结果
enum HTTP_CODE {
    NO_REQUEST = 0,    // 请求不完整，需要继续读取客户数据
    GET_REQUEST,       // 获得了一个完整的客户请求
    BAD_REQUEST,       // 客户请求有语法错误
    FORBIDDEN_REQUEST, // 客户对资源没有足够的访问权限
    INTERNAL_ERROR,    // 服务器内部错误
    CLOSED_CONNECTION, // 客户端已经关闭了连接
};

// 服务器给客户端的应答信息（这里做了简化处理，只是发送成功或失败的信息，
// 而不是发送完整的应答报文）
static const char * szret[] = {
    "I get a correct result\n",
    "Something wrong\n"
};

// 从状态机，用于解析出一行内容
LINE_STATUS
parse_line(char * buffer, int &checked_index, int &read_index) {
    char temp;
    for (; checked_index < read_index; ++checked_index) {
        temp = buffer[checked_index];
        if (temp == '\r') {
            if (checked_index + 1 == read_index) {
                return LINE_OPEN;
            } else if (buffer[checked_index+1] == '\n') {
                buffer[checked_index++] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if (temp == '\n') {
            if (checked_index>1 && buffer[checked_index-1]=='\r') {
                buffer[checked_index-1] = '\0';
                buffer[checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 如果所有内容都分析完毕都没有遇到'\r'，则返回'LINE_OPEN'，表示
    // 还需要继续读取客户端数据才能进一步分析
    return LINE_OPEN;
}

// 分析请求行
HTTP_CODE
parse_requestline(char * temp, CHECK_STATE &check_state) {
    char * url = strpbrk(temp, " \t");
    // 如果请求行中没有空格或'\t'，则说明HTTP请求有问题
    if (!url) {
        return BAD_REQUEST;
    }
    *url++ = '\0';

    char * method = temp;
    if (strcasecmp(method, "GET") == 0) {  // 仅支持GET
        printf("The request method is GET\n");
    } else {
        return BAD_REQUEST;
    }

    url += strspn(url, " \t");
    char * version = strpbrk(url, " \t");
    if (!version) {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0) { // 仅支持HTTP/1.1
        return BAD_REQUEST;
    }

    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;
        url = strchr(url, '/');
    }

    if (!url || url[0]!='/') {
        return BAD_REQUEST;
    }
    printf("The request URL is: %s\n", url);

    // HTTP请求行处理完毕，状态转移到头部字段的分析
    check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 分析头部字段
HTTP_CODE
parse_header(char * temp) {
    if (temp[0] == '\0') {
        return GET_REQUEST;
    } else if (strncasecmp(temp, "Host:", 5) == 0) {
        temp += 5;
        temp += strspn(temp, " \t");
        printf("The request host is: %s\n", temp);
    } else {
        printf("I can not handle this header\n");
    }
    return NO_REQUEST;
}

HTTP_CODE
parse_content(char * buffer, int &checked_index, int &read_index,
CHECK_STATE &check_state, int &start_line) {
    LINE_STATUS line_status = LINE_OK;  // 记录当前行的读取状态
    HTTP_CODE ret_code = NO_REQUEST;    // 记录HTTP请求的处理结果
    while (
        (line_status = parse_line(buffer, checked_index, read_index)) == LINE_OK
    ) {
        char * temp = buffer + start_line;
        start_line = checked_index;   // 记录下一行的起始位置
        switch (check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret_code = parse_requestline(temp, check_state);
                if (ret_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            } 
            case CHECK_STATE_HEADER: {
                ret_code = parse_header(temp);
                if (ret_code == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret_code == GET_REQUEST) {
                    return GET_REQUEST;
                }
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    if (line_status == LINE_OPEN) {
        return NO_REQUEST;
    } else {
        return BAD_REQUEST;
    }
}

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
    address.sin_port = htons(port);
    inet_pton(AF_INET, ip, &address.sin_addr);

    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    int ret = bind(listen_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listen_fd, 5);
    assert(ret != -1);

    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int fd = accept(listen_fd, (sockaddr *)&client, &client_addr_len);
    if (fd < 0) {
        printf("errno is: %d\n", errno);
    } else {
        char buffer[BUFFER_SIZE];  // 读缓冲区
        memset(buffer, '\0', BUFFER_SIZE);
        int data_read = 0;
        int checked_index = 0;   // 当前已经分析了多少字节的客户数据
        int read_index = 0;      // 当前已经读取了多少字节的客户数据
        int start_line = 0;      // 行在buffer中起始位置 
        CHECK_STATE check_state = CHECK_STATE_REQUESTLINE;
        while (true) {  // 循环读取用户数据并分析
            data_read = recv(fd, buffer+read_index, BUFFER_SIZE-read_index, 0);
            if (data_read == -1) {
                printf("reading failed\n");
                break;
            } else if (data_read == 0) {
                printf("remote client has closed the connection\n");
                break;
            }
            read_index += data_read;
            HTTP_CODE res = parse_content(buffer, checked_index, read_index, check_state, start_line);
            if (res == NO_REQUEST) {
                continue;
            } else if (res == GET_REQUEST) {
                send(fd, szret[0], strlen(szret[0]), 0);
                break;
            } else {
                send(fd, szret[1], strlen(szret[1]), 0);
                break;
            }
        }
        close(fd);
    }

    close(listen_fd);
    return 0;
}