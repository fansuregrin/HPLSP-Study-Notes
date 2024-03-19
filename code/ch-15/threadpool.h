/**
 * @file threadpool.h
 * @author
 * @date 2024-03-17
 * @brief 半同步/半反应堆线程池
*/
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <cstdio>
#include <list>
#include <exception>
#include "../ch-14/locker.h"

/**
 * @brief 线程池类
 * 
 * 模板参数 T 是任务类。
*/
template <typename T>
class threadpool {
public:
    threadpool(int thread_number=8, int max_requests=10000);
    ~threadpool();
public:
    bool append(T * request);
private:
    static void * worker(void * arg);
    void run();
private:
    int m_thread_number;        // 线程池中的线程数量
    int m_max_requests;         // 请求队列中允许的最大任务请求数量
    pthread_t * m_threads;      // 描述线程池的数组
    std::list<T *> m_workqueue; // 任务请求队列
    locker m_queuelocker;       // 保护任务请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                // 是否结束线程
};

/**
 * @brief 线程池构造函数
 * @param thread_number 线程池中的线程数量
 * @param max_requests 请求队列中的最大任务数量
*/
template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests)
: m_thread_number(thread_number), m_max_requests(max_requests),
  m_stop(false), m_threads(nullptr) {
    if (thread_number<=0 || max_requests<=0) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    for (int i=0; i<m_thread_number; ++i) {
        printf("create the %dth thread\n", i);
        if (pthread_create(m_threads+i, nullptr, worker, this)) {
            delete [] m_threads;
            throw std::exception();
        }
        // 设置为脱离线程
        if (pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
    m_stop = true;
}

/**
 * @brief 向请求队列中添加一个任务
 * @param request 需要添加的任务
 * @return 是否添加成功
*/
template <typename T>
bool threadpool<T>::append(T * request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/**
 * @brief 工作线程运行的函数
 * @param arg 线程池对象
 * @return 
*/
template <typename T>
void * threadpool<T>::worker(void * arg) {
    threadpool * pool = (threadpool *)arg;
    pool->run();
    return pool;
}

/**
 * @brief 
*/
template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T * request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        request->process();
    }
}

#endif