/**
 * @file access_daytime_serv.cpp
 * @author
 * @date 2024-03-07
 * @brief 访问daytime服务
*/
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cassert>

int main(int argc, char * argv[]) {
    assert(argc == 2);
    const char * host = argv[1];

    // get target host address information
    hostent * hostinfo = gethostbyname(host);
    assert(hostinfo);
    // get daytime service information
    servent * servinfo = getservbyname("daytime", "tcp");
    assert(servinfo);
    printf("daytime port is %d\n", ntohs(servinfo->s_port));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_port = servinfo->s_port;
    address.sin_addr = *(in_addr *)*(hostinfo->h_addr_list);

    int sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(sock_fd >= 0);
    int ret = connect(sock_fd, (sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    char buffer[128];
    ret = read(sock_fd, buffer, sizeof(buffer));
    assert(ret > 0);
    buffer[ret] = '\0';
    printf("the daytime is: %s\n", buffer);

    close(sock_fd);
    return 0;
}