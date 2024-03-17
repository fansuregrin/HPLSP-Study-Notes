/**
 * @file test_get_addrinfo.cpp
 * @author
 * @date 2024-03-07
 * @brief 使用 getaddrinfo 函数
*/
#include <netdb.h>
#include <cstring>
#include <cstdio>

int main() {
    addrinfo hints;
    addrinfo * res = nullptr;

    bzero(&hints, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    getaddrinfo("FG-Server02", "daytime", &hints, &res);
    freeaddrinfo(res);
}