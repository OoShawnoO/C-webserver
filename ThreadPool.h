#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <iostream>
#include "locker.h"

template<typename T>
class threadpool{
public:
    threadpool(int thread_number = 8,int max_requests = 10000);
    ~threadpool();
    bool append(T* requests);
private:
    static void* worker(void* arg);
    void run();
    int m_thread_number;
    pthread_t* m_threads; // 线程池
    int m_max_requests; //最大请求数量
    std::list<T*> m_workqueue;  //请求队列
    locker m_queuelocker;
    sem m_queuestat;
    bool m_stop;
};

template <typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):m_thread_number(thread_number),m_max_requests(max_requests),m_stop(false),m_threads(NULL)
{
    if((thread_number<=0)||(max_requests<=0)){
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads){
        throw std::exception();
    }
    for(int i=0;i<thread_number;++i){
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete[] m_threads;
            throw std::exception();
        }

        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* requests){
    m_queuelocker.lock();
    if(m_workqueue.size()>m_max_requests){
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(requests);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
    threadpool* pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request){
            continue;
        }

        request->process();
    }
}

#endif