/**
 * @file my_tee.cpp
 * @author
 * @date 2024-03-08
 * @brief 同时输出数据到终端和文件
*/
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc, char * argv[]) {
    if (argc != 2) {
        printf("usage: %s <file>\n", basename(argv[0]));
        return 1;
    }

    int filefd = open(argv[1], O_CREAT | O_WRONLY | O_TRUNC, 0666);
    assert(filefd > 0);

    int pipefd_stdout[2];
    int ret = pipe(pipefd_stdout);
    assert(ret != -1);

    int pipefd_file[2];
    ret = pipe(pipefd_file);
    assert(ret != -1);

    // 将标准输入的内容输入管道pipefd_stdout
    ret = splice(STDIN_FILENO, nullptr, pipefd_stdout[1], nullptr,
                    32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);
    // 将管道pipefd_stdout的输出复制到管道pipefd_file的输入端
    ret = tee(pipefd_stdout[0], pipefd_file[1], 32768, SPLICE_F_NONBLOCK);
    assert(ret != -1);
    // 将管道pipefd_file的输出定向到文件描述符filefd上，从而实现将标准输入的内容写入文件
    ret = splice(pipefd_file[0], nullptr, filefd, nullptr,
                    32768, SPLICE_F_MOVE | SPLICE_F_MORE);
    assert(ret != -1);
    // 将管道pipefd_stdout的输出定向到标准输出，从而实现将同样的内容写入标准输出
    ret = splice(pipefd_stdout[0], nullptr, STDOUT_FILENO, nullptr,
                    32768, SPLICE_F_MORE | SPLICE_F_MOVE);
    assert(ret != -1);

    close(filefd);
    close(pipefd_stdout[0]);
    close(pipefd_stdout[1]);
    close(pipefd_file[0]);
    close(pipefd_file[1]);

    return 0;
}