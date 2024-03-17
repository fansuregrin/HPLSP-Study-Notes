/**
 * @file ip_addr_convert.cpp
 * @author
 * @date 2024-03-06
 * @brief IP地址转换
*/
#include <netinet/in.h>
#include <bits/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <iostream>
#include <cstdio>

int main() {
    using std::cout;
    in_addr a, b;
    auto in_addr_ = inet_addr("10.140.32.106");
    printf("%x\n", in_addr_);
    inet_aton("10.140.32.106", &a);
    inet_aton("127.0.0.1", &b);

    // `pa1` and `pa2` point to the same memory space.
    // The `inet_ntoa` uses a `static` memory space to store the result.
    auto pa1 = inet_ntoa(a);
    auto pa2 = inet_ntoa(b);
    printf("a: %s\n", pa1);
    printf("b: %s\n", pa2);

    in_addr_t res1;
    inet_pton(AF_INET, "10.140.32.106", &res1);
    printf("%x\n", res1);

    char * res2 = new char[INET_ADDRSTRLEN];
    auto pa3 = inet_ntop(AF_INET, &res1, res2, INET_ADDRSTRLEN);
    if (pa3) {
        printf("%s\n", pa3);
    }
    delete [] res2;
}