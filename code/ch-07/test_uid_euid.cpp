/**
 * @file test_uid_euid.cpp
 * @author
 * @date 2024-03-08
 * @brief 测试进程的UID和EUID的区别
*/
#include <unistd.h>
#include <cstdio>

int main() {
    auto uid = getuid();
    auto euid = geteuid();
    printf("user-id is [%d], effective user-id is [%d]\n", uid, euid);
    return 0;
}