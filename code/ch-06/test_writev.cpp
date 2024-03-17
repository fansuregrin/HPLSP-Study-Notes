/**
 * @file test_writev.cpp
 * @author
 * @date 2024-03-07
 * @brief Web 服务器上的集中写
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cerrno>

#define BUFFER_SIZE 1024

static const char * status_line[2] = {"200 OK", "500 Internal server error"};

int main(int argc, char * argv[]) {
    if (argc <= 3) {
        printf("usage: %s ip_address port_number filename\n", basename(argv[0]));
        return 1;
    }
    const char * ip = argv[1];
    int port = atoi(argv[2]);
    const char * filename = argv[3];

    sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int sock = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    int ret = bind(sock, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(sock, 5);
    assert(ret != -1);

    sockaddr_in client;
    socklen_t client_addr_len = sizeof(client);
    int conn_fd = accept(sock, (sockaddr *)&client, &client_addr_len);
    if (conn_fd < 0) {
        printf("errno is: %d\n", errno);
    } else {
        // 用于保存HTTP应答的状态行、头部字段和一个空行的缓冲区
        char header_buffer[BUFFER_SIZE];
        memset(header_buffer, '\0', BUFFER_SIZE);
        // 用于存在目标文件内容的应用程序缓存
        char * file_buf;
        // 用于获取目标文件的属性，比如是否为目录、文件大小等
        struct stat file_stat;
        // 记录目标文件是否是有效文件
        bool valid = true;
        // 缓冲区header_buffer目前已经使用了多少字节的空间
        int len = 0;
        if (stat(filename, &file_stat) < 0) { // 目标文件不存在
            valid = false;
        } else {
            if (S_ISDIR(file_stat.st_mode)) { // 目标文件是一个目录
                valid = false;
            } else if (file_stat.st_mode & S_IROTH) { // 当前用户有读取目标文件的权限
                int fd = open(filename, O_RDONLY);  // 只读方式打开文件
                file_buf = new char[file_stat.st_size+1];  // 动态分配缓冲区来保存文件内容
                memset(file_buf, '\0', file_stat.st_size+1);
                if (read(fd, file_buf, file_stat.st_size+1) < 0) { // 读失败
                    valid = false;
                }
            } else {
                valid = false;
            }
        }
        // 如果目标文件有效，则发送正常的HTTP应答
        if (valid) {
            // 将HTTP应答的状态行、“Content-Length”头部字段和一个空行依次加入到header_buffer
            ret = snprintf(header_buffer, BUFFER_SIZE-1, "%s %s\r\n",
                           "HTTP/1.1", status_line[0]);
            len += ret;
            ret = snprintf(header_buffer+len, BUFFER_SIZE-1-len, "Content-Length: %d\r\n",
                            file_stat.st_size);
            len += ret;
            ret = snprintf(header_buffer+len, BUFFER_SIZE-1-len, "\r\n");
            struct iovec iv[2];
            iv[0].iov_base = header_buffer;
            iv[0].iov_len = strlen(header_buffer);
            iv[1].iov_base = file_buf;
            iv[1].iov_len = file_stat.st_size;
            ret = writev(conn_fd, iv, 2);
        } else {  // 如果目标文件无效，则通知客户端服务器发生了内部错误
            ret = snprintf(header_buffer, BUFFER_SIZE-1, "HTTP/1.1 %s\r\n", status_line[1]);
            len += ret;
            ret = snprintf(header_buffer, BUFFER_SIZE-1-len, "\r\n");
            send(conn_fd, header_buffer, strlen(header_buffer), 0);
        }
        close(conn_fd);
        delete [] file_buf;
    }
    
    close(sock);
    return 0;
}