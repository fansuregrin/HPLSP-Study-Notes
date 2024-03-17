/**
 * @file lst_timer.h
 * @author
 * @date 2024-03-11
 * @brief 升序定时器链表。
*/
#ifndef LST_TIMER
#define LST_TIMER

#include <ctime>
#include <netinet/in.h>
#include <cstdio>

#define BUFFER_SIZE 64

class util_timer;  // forward declaration

// 用户数据结构
struct client_data {
    sockaddr_in address;    // 客户端 socket 地址
    int sockfd;             // socket 文件描述符
    char buf[BUFFER_SIZE];  // 读缓冲区
    util_timer * timer;     // 定时器
};

// 定时器类
class util_timer {
public:
    util_timer(): prev(nullptr), next(nullptr) {}
public:
    time_t expire;      // 超时时间
    void (*cb_func)(client_data *);  // 任务回调函数
    client_data * user_data;  // 回调函数处理的客户数据，由定时器的执行者传递给回调函数
    util_timer * prev;
    util_timer * next;
};

// 定时器链表。它是一个升序、双向且带有头结点和尾结点的链表。
class sort_timer_lst {
public:
    sort_timer_lst(): head(nullptr), tail(nullptr) {}

    ~sort_timer_lst() {
        util_timer * tmp = head;
        while (tmp) {
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }
public:
    // 将目标定时器 timer 添加到链表中
    void add_timer(util_timer * timer) {
        if (!timer) return;
        if (!head) {
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire) {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    // 当某个定时任务发生变化时，调整对应的定时器在链表中的位置。
    // 只考虑被调整的定时器的超时时间延长的情况。
    void adjust_timer(util_timer * timer) {
        if (!timer) return;
        util_timer * tmp = timer->next;
        // 如果被调整的定时器处在链表的尾部，或者该定时器的超时时间仍然小于其下一个
        // 定时器的超时时间，则不需要调整
        if (!tmp || timer->expire < tmp->expire) return;
        
        // 如果目标定时器是链表的头结点，则将该定时器从链表中取出并重新插入其后面的
        // 剩余结点组成的链表。
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            timer->next = nullptr;
            add_timer(timer, head);
        } else { 
            // 如果目标定时器不是链表的头结点，则将该定时器从链表中取出并重新插入其后面的
            // 剩余结点组成的链表。
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void delete_timer(util_timer * timer) {
        if (!timer) return;
        // 链表中只有一个定时器，即要删除的目标定时器
        if (timer == head && timer == tail) {
            delete timer;
            head = tail = nullptr;
            return;
        }
        // 链表中至少有两个定时器，其目标定时器是头结点
        if (timer == head) {
            head = head->next;
            head->prev = nullptr;
            delete timer;
            return;
        }
        // 链表中至少有两个定时器，其目标定时器是尾结点
        if (timer == tail) {
            tail = tail->prev;
            tail->next = nullptr;
            delete timer;
            return;
        }
        // 如果目标定时器位于链表中间，则把它前后的定时器串联起来，
        // 然后删除目标定时器
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    // SIGALRM 信号每次被触发就在其信号处理函数中执行依次 tick 函数，
    // 以处理链表上到期的任务
    void tick() {
        if (!head) return;
        printf("timer tick\n");
        time_t cur = time(nullptr);  // 获取系统当前时间
        util_timer * tmp = head;
        while (tmp) {
            if (cur < tmp->expire) break;
            // 调用定时器中的回调函数，以执行定时任务
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head) {
                head->prev = nullptr;
            }
            delete tmp;
            tmp = head;
        }
    }
private:
    // 将目标定时器 timer 插入到 lst_head 之后的部分链表中
    void add_timer(util_timer * timer, util_timer * lst_head) {
        if (!timer) return;
        util_timer * prev = head;
        util_timer * tmp = prev->next;
        // 遍历 lst_head 之后的部分链表，直到找到一个超时时间大于目标定时器的超时时间的
        // 结点，并将目标定时器插入该定时器之前
        while (tmp) {
            if (timer->expire < tmp->expire) {
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        // 遍历完 lst_head 之后的部分链表后，仍未找到超时时间大于目标定时器的超时时间的
        // 结点，则将目标定时器插入链表尾部，并将其设为尾结点。
        if (!tmp) {
            prev->next = timer;
            timer->next = nullptr;
            timer->prev = prev;
            tail = timer;
        }
    }
private:
    util_timer * head;
    util_timer * tail;
};

#endif