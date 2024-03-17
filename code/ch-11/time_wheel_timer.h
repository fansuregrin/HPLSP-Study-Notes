/**
 * @file time_wheel_timer.h
 * @author
 * @date 2024-03-12
 * @brief 时间轮
*/
#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

#include <netinet/in.h>
#include <ctime>
#include <cstdio>

#define BUFFER_SIZE 64

class tw_timer;

struct client_data {
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer * timer;
};

class tw_timer {
public:
    tw_timer(int rot, int ts)
    : rotation(rot), time_slot(ts), prev(nullptr), next(nullptr) {}
public:
    int rotation;   // 记录定时器在时间轮转动多少圈后生效
    int time_slot;  // 记录定时器属于时间轮的哪个槽
    void (*cb_func)(client_data *);  // 定时器回调函数
    client_data * user_data;         // 客户数据
    tw_timer * prev;
    tw_timer * next;
};

class time_wheel {
public:
    time_wheel(): cur_slot(0) {
        for (int i=0; i<N; ++i) {
            slots[i] = nullptr;  // 初始化每个槽的头节点
        }
    }

    ~time_wheel() {
        // 遍历每个槽，并删除其中的定时器
        for (int i=0; i<N; ++i) {
            tw_timer * tmp = slots[i];
            while (tmp) {
                slots[i] = tmp->next;
                delete tmp;
                tmp = slots[i];
            }
        }
    }

    // 根据定时值 timeout 来创建一个定时器，并把它插入到合适的槽中
    tw_timer * add_timer(int timeout) {
        if (timeout < 0) return nullptr;
        // 根据待插入的定时器的定时值 timeout 来计算它将在时间轮转动多少个滴答后被触发，
        // 并将滴答数存储在变量 ticks 中。如果 timeout 小于时间轮的槽间隔时间，则将
        // ticks 向上折合为1；否则，就将 ticks 向下折合为 timeout/SI。
        int ticks = 0;
        if (timeout < SI) {
            ticks = 1;
        } else {
            ticks = timeout / SI;
        }
        // 计算待插入的定时器在时间轮转动了多少圈后被触发
        int rotation = ticks / N;
        // 计算待插入的定时器应该被插入到哪个槽中
        int ts = (cur_slot + ticks%N) % N;
        tw_timer * timer = new tw_timer(rotation, ts);
        // 如果第 ts 个时间槽中尚无任何定时器，则将新创建的定时器插入其中，并将该
        // 定时器设置为该槽的头结点。
        if (!slots[ts]) {
            printf("add timer, rotation is %d, ts slot is %d, cur_slot is %d\n",
                rotation, ts, cur_slot);
            slots[ts] = timer;
        } else {
            slots[ts]->prev = timer;
            timer->next = slots[ts];
            slots[ts] = timer;
        }
        return timer;
    }

    // 删除目标定时器 timer
    void del_timer(tw_timer * timer) {
        if (!timer) return;
        int ts = timer->time_slot;
        if (slots[ts] == timer) {
            slots[ts] = slots[ts]->next;
            if (slots[ts]) {
                slots[ts]->prev = nullptr;
            }
            delete timer;
        } else {
            timer->prev->next = timer->next;
            if (timer->next) {
                timer->next->prev = timer->prev;
            }
            delete timer;
        }
    }

    // SI 时间到后，调用该函数，时间轮向前滚动一个槽的间隔
    void tick() {
        tw_timer * tmp = slots[cur_slot];
        printf("current slot is %d\n", cur_slot);
        while (tmp) {
            printf("tick the timer once\n");
            if (tmp->rotation > 0) {
                tmp->rotation--;
                tmp = tmp->next;
            } else {
                tmp->cb_func(tmp->user_data);
                if (tmp == slots[cur_slot]) {
                    printf("delete header in current slot\n");
                    slots[cur_slot] = slots[cur_slot]->next;
                    delete tmp;
                    if (slots[cur_slot]) {
                        slots[cur_slot]->prev = nullptr;
                    }
                    tmp = slots[cur_slot];
                } else {
                    tmp->prev->next = tmp->next;
                    if (tmp->next) {
                        tmp->next->prev = tmp->prev;
                    }
                    tw_timer * tmp2 = tmp->next;
                    delete tmp;
                    tmp = tmp2;
                }
            }
        }
        cur_slot = ++cur_slot % N;
    }
private:
    static const int N = 60;  // 时间轮上槽的个数
    static const int SI = 1;  // 槽间隔 (slot interval) 为 1s
    tw_timer * slots[N];      // 时间轮的槽，其中每个元素指向一个定时器链表
    int cur_slot;             // 时间轮的当前槽
};

#endif