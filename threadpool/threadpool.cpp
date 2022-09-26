#include "threadpool.h"

ThreadPool::ThreadPool(int threadNum) : m_threadNum(threadNum){
    // 初始化互斥量
    int ret = pthread_mutex_init(&queueLocker, nullptr);
    if(ret != 0){
        throw std::runtime_error("初始化互斥量失败");
    }

    // 初始化信号量
    ret = sem_init(&queueEventNum, 0, 0);
    if(ret != 0){
        throw std::runtime_error("初始化信号量失败");
    }

    // 初始化线程池中的所有线程
    m_threads = new pthread_t[m_threadNum];
    for(int i = 0; i < m_threadNum; ++i){
        ret = pthread_create(m_threads + i, nullptr, worker, this);
        if(ret != 0){
            delete[] m_threads;
            throw std::runtime_error("线程创建失败");
        }
        ret = pthread_detach(m_threads[i]);
        ++tnum;
        usleep(1000);     // 为了在线程中记录线程序号
        if(ret != 0){
            delete[] m_threads;
            throw std::runtime_error("设置脱离线程失败");
        }
    }
}


ThreadPool::~ThreadPool(){
    // 释放互斥量
    pthread_mutex_destroy(&queueLocker);
    
    // 释放信号量
    sem_destroy(&queueEventNum);
    
    // 释放动态创建的保存线程 id 的数组
    delete[] m_threads;

}

int ThreadPool::appendEvent(EventBase* event, const std::string eventType){
    int ret = 0;
    // 事件队列加锁
    ret = pthread_mutex_lock(&queueLocker);
    if(ret != 0){
        std::cout << outHead("error") << "事件队列加锁失败" << std::endl;
        return -1;
    }
    // 向队列中添加事件
    m_workQueue.push(event);
    std::cout << outHead("info") << eventType << "添加成功，线程池事件队列中剩余的事件个数：" << m_workQueue.size() << std::endl;
    // 事件队列解锁
    pthread_mutex_unlock(&queueLocker);
    if(ret != 0){
        std::cout << outHead("error") << "事件队列解锁失败" << std::endl;
        return -2;
    }
    // 事件队列的信号量加一
    sem_post(&queueEventNum);
    if(ret != 0){
        std::cout << outHead("error") << "事件队列信号量 post 失败" << std::endl;
        return -3;
    }
    
    return 0;
}


void *ThreadPool::worker(void *arg){
    ThreadPool *thiz = static_cast<ThreadPool*>(arg);
    thiz->run();
    return thiz;
}


void ThreadPool::run(){
    int threadN = tnum;
    std::cout << outHead("info") << "线程 " << threadN << " 正在执行" << std::endl;
    while(1){
        // 等待事件队列中有新的事件
        int ret = sem_wait(&queueEventNum);
        if(ret != 0){
            std::cout << outHead("error") << "等待队列事件失败" << std::endl;
            return;
        }
        std::cout << outHead("log") << "线程 " << threadN << " 收到事件" << std::endl;
        // 互斥访问队列
        ret = pthread_mutex_lock(&queueLocker);
        if(ret != 0){
            std::cout << outHead("error") << "ThreadPool:run() : 事件队列加锁失败" << std::endl;
            return;
        }
        // 获取最前面的事件
        EventBase* curEvent = m_workQueue.front();
        m_workQueue.pop();
        
        // 解锁访问队列
        ret = pthread_mutex_unlock(&queueLocker);
        if(ret != 0){
            std::cout << outHead("error") << "ThreadPool:run() : 事件队列解锁失败" << std::endl;
            return;
        }

        if(curEvent == nullptr){
            continue;
        }
        std::cout << outHead("info") << "线程 " << threadN << " 开始处理事件" << std::endl;
        curEvent->process();
        std::cout << outHead("info") << "线程 " << threadN << " 处理事件完成" << std::endl;
        // 事件执行完需要销毁
        delete curEvent;
    }
}
