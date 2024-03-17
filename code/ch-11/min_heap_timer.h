/**
 * @file min_heap_timer.h
 * @author
 * @date 2024-03-13
 * @brief 时间堆
*/
#ifndef MIN_HEAP_TIMER
#define MIN_HEAP_TIMER

#include <exception>
#include <netinet/in.h>
#include <ctime>

#define BUFFER_SIZE 64

using std::exception;

class heap_timer;

struct client_data {
    sockaddr_in address;
    int sockfd;
    heap_timer * timer;
    char buf[BUFFER_SIZE];
};

class heap_timer {
public:
    heap_timer(int delay) {
        expire = time(nullptr) + delay;
    }
public:
    time_t expire;                     // 定时器生效的绝对时间
    void (*cb_func)(client_data *);    // 定时器的回调函数
    client_data * user_data;           // 用户数据
};

class time_heap {
public:
    // 初始化一个容量为 cap 的空堆。
    time_heap(int cap): capacity(cap), cur_size(0) {
        array = new heap_timer* [capacity];
        if (!array) throw exception();
        for (int i=0; i<capacity; ++i) {
            array[i] = nullptr;
        }
    }

    // 用已有数组来初始化堆
    time_heap(heap_timer* *init_array, int size, int cap)
    : cur_size(size), capacity(cap) {
        if (capacity < cur_size) {
            throw exception();
        }
        array = new heap_timer* [capacity];
        if (!array) {
            throw exception();
        }
        for (int i=0; i<capacity; ++i) {
            array[i] = nullptr;
        }
        if (cur_size != 0) {
            for (int i=0; i<cur_size; ++i) {
                array[i] = init_array[i];
            }
            // 对数组中的第⌊(cur_size-1)/2⌋~0个元素执行下虑操作
            for (int i=(cur_size-1)/2; i>=0; --i) {
                percolate_down(i);
            }
        }
    }

    ~time_heap() {
        for (int i=0; i<cur_size; ++i) {
            delete array[i];
        }
        delete [] array;
    }
public:
    // 添加目标定时器
    void add_timer(heap_timer * timer) {
        if (!timer) return;
        if (cur_size >= capacity) resize();
        int hole = cur_size++;
        int parent = 0;
        for (; hole > 0; hole = parent) {
            parent = (hole - 1) / 2;
            if (array[parent]->expire <= timer->expire) {
                break;
            }
            array[hole] = array[parent];
        }
        array[hole] = timer;
    }

    // 删除目标定时器
    void del_timer(heap_timer * timer) {
        if (!timer) return;
        // 仅仅将目标定时器的回调函数设为空，即所谓的延迟销毁。这将节省真正删除
        // 该定时器造成的开销，但这样做容易使堆数组膨胀。
        timer->cb_func = nullptr;
    }

    // 获取堆顶部的定时器
    heap_timer * top() const {
        if (empty()) return nullptr;
        return array[0];
    }

    // 删除堆顶部的定时器
    void pop_timer() {
        if (empty()) return;
        delete array[0];
        array[0] = nullptr;
        if (cur_size > 1) {
            array[0] = array[cur_size-1];
        }
        if (array[0]) {
            percolate_down(0);
        }
        --cur_size;
    }

    // 心搏函数
    void tick() {
        heap_timer * tmp = top();
        time_t cur = time(nullptr);
        while (!empty()) {
            if (!tmp) break;
            if (tmp->expire > cur) break;
            if (tmp->cb_func) {
                tmp->cb_func(tmp->user_data);
            }
            pop_timer();
            tmp = top();
        } 
    }

    bool empty() const {
        return cur_size == 0;
    }
private:
    // 最小堆的下虑操作，它确保堆数组中以 hole 为根结点的子树拥有最小堆的性质。
    void percolate_down(int hole) {
        heap_timer * tmp = array[hole];
        int child = 0;
        for (; hole*2+1 <= cur_size-1; hole = child) {
            child = hole * 2 + 1;
            if ((child < cur_size-1) && 
                (array[child+1]->expire < array[child]->expire)) {
                ++child;
            }
            if (array[child]->expire < tmp->expire) {
                array[hole] = array[child];
            } else {
                break;
            }
        }
        array[hole] = tmp;
    }

    // 将堆数组的容量扩大一倍
    void resize() {
        heap_timer* *tmp = new heap_timer* [capacity * 2];
        if (!tmp) {
            throw exception();
        }
        capacity *= 2;
        for (int i=0; i<capacity; ++i) {
            tmp[i] = nullptr;
        }
        for (int i=0; i<cur_size; ++i) {
            tmp[i] = array[i];
        }
        delete [] array;
        array = tmp;
    }
private:
    heap_timer* *array;   // 堆数组
    int capacity;         // 堆数组的容量
    int cur_size;         // 堆数组当前的元素个数
};

#endif