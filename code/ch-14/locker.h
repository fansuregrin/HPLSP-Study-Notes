/**
 * @file locker.h
 * @author
 * @date 2024-03-15
 * @brief 线程同步机制包装类
*/
#ifndef LOCKER_H
#define LOCKER_H

#include <semaphore.h>
#include <pthread.h>
#include <exception>

/**
 * @brief 封装信号量的类。
*/
class sem {
public:
    /**
     * @brief 创建并初始化信号量。
    */
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }

    /**
     * @brief 销毁信号量。
    */
    ~sem() {
        sem_destroy(&m_sem);
    }

    /**
     * @brief 等待信号量。
    */
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    /**
     * @brief 增加信号量。
    */
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};


/**
 * @brief 封装互斥锁的类。
*/
class locker {
public:
    /**
     * @brief 创建并初始化互斥锁。
    */
    locker() {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception();
        }
    }

    /**
     * @brief 销毁互斥锁。
    */
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    /**
     * @brief 获取互斥锁。
    */
    bool lock() {
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    /**
     * @brief 释放互斥锁。
    */
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
private:
    pthread_mutex_t m_mutex;
};


/**
 * @brief 封装条件变量的类。
*/
class cond {
public:
    /**
     * @brief 创建并初始化条件变量。
    */
    cond() {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::exception();
        }
        if (pthread_cond_init(&m_cond, nullptr) != 0) {
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }

    /**
     * @brief 销毁条件变量。
    */
    ~cond() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }

    /**
     * @brief 等待条件变量。
    */
    bool wait() {
        int ret = 0;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }

    /**
     * @brief 唤醒等待条件变量的线程。
    */
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif