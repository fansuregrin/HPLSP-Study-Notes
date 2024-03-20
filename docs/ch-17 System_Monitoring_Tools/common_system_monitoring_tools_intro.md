# 类Unix平台常见系统监测工具介绍

## tcpdump

[Tcpdump](https://www.tcpdump.org/) 是一个用于分析网络上的包的命令行工具 (packet analyzer)，其功能很多，具体可以使用`man tcpdump`来查看其手册。

## lsof

[lsof](https://github.com/lsof-org/lsof)(**L**i**S**t **O**pen **F**iles) 是一个用于列出系统中打开的文件的工具。

例如，我可以使用它来查看`web_server`这个进程打开的文件。
```
[fansuregrin@FG-Server02]$ sudo lsof -p `pidof web_server`
COMMAND      PID        USER   FD      TYPE  DEVICE SIZE/OFF    NODE NAME
web_serve 707428 fansuregrin  cwd       DIR   254,3     4096  920486 /home/fansuregrin/workshop/server_dev_learning/code/ch-15
web_serve 707428 fansuregrin  rtd       DIR   254,3     4096       2 /
web_serve 707428 fansuregrin  txt       REG   254,3   113416  921229 /home/fansuregrin/workshop/server_dev_learning/code/ch-15/bin/web_server
web_serve 707428 fansuregrin  mem       REG   254,3  1922136 1079573 /usr/lib/x86_64-linux-gnu/libc.so.6
web_serve 707428 fansuregrin  mem       REG   254,3  2190440 1048757 /usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.30
web_serve 707428 fansuregrin  mem       REG   254,3   907784 1079576 /usr/lib/x86_64-linux-gnu/libm.so.6
web_serve 707428 fansuregrin  mem       REG   254,3   125312 1046540 /usr/lib/x86_64-linux-gnu/libgcc_s.so.1
web_serve 707428 fansuregrin  mem       REG   254,3   210968 1079570 /usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
web_serve 707428 fansuregrin    0u      CHR   136,7      0t0      10 /dev/pts/7
web_serve 707428 fansuregrin    1u      CHR   136,7      0t0      10 /dev/pts/7
web_serve 707428 fansuregrin    2u      CHR   136,7      0t0      10 /dev/pts/7
web_serve 707428 fansuregrin    3u     IPv4 3535860      0t0     TCP FG-Server02:12345 (LISTEN)
web_serve 707428 fansuregrin    4u  a_inode    0,14        0      28 [eventpoll:3]
```
从上面的结果可以看出`web_server`一共打开了13个文件。**FD**那一列表示文件描述符的具体描述，**TYPE**表示文件描述符的类型，**DEVICE**表示文件所属的设备，**SIZE/OFF**表示文件大小或者偏移值（如果字段显示为"`0t*`"或"`0x*`"则表示偏移值，否则就是文件大小），**NODE**表示文件的i节点号（对于socket，表示协议类型），**NAME**表示文件的名称。不难看出，`web_server`打开了工作目录(FD显示为`cwd`(current work directory))；打开了根目录(FD显示为`rtd`(root directory))；打开了代表自身的可执行文件(FD显示为`txt`)；同时，还打开了5个共享库(动态链接库，share-object，FD显示为`mem`)；打开了标准输入文件(stdin, FD显示为`0u`)；打开了标准输出文件(stdout, FD显示为`1u`)；打开了标准错误输出文件(stderr, FD显示为`2u`)。倒数第2行表示`web_server`打开的监听socket，FD显示为`3u`，TYPE显示为`IPv4`，协议是`TCP`；最后一行表示`web_server`创建的用来标识epoll内核事件表的那个文件描述符。

## nc
[nc](https://netcat.sourceforge.net/) 主要用来建立网络连接。它可以作为服务端来监听某个端口并接收客户连接；也可以作为客户端来向服务器发起连接。

例如：使用`nc 127.0.0.1 -l -p 54321`来监听本地的`54321`端口，再使用`telnet 127.0.0.1 54321`向这个监听服务发起连接。结果如下：
```
# listen
[fansuregrin@FG-Server02]$ nc 127.0.0.1 -l -p 54321
hi

# connect
[fansuregrin@FG-Server02]$ telnet 127.0.0.1 54321
Trying 127.0.0.1...
Connected to 127.0.0.1.
Escape character is '^]'.
^]
telnet>
hi
```
可以看到客户发送"hi"，服务端也成功接收到了。

例如，还可以使用`nc -C 172.28.119.110 12345`向服务端发送HTTP请求。
```
[fansuregrin@FG-Server02]$ nc -C 172.28.119.110 12345
GET /hi.html HTTP/1.1
Accept: */*

HTTP/1.1 200 OK
Content-Length: 6
Connection: close

hi!!!
```
结果显示，服务器成功给客户返回了请求的内容。

## strace
[strace](https://strace.io/) 可以跟踪程序运行过程中执行的系统调用和接收到的信号。