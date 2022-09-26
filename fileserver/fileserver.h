#ifndef WEBSERVER_H
#define WEBSERVER_H
#include <signal.h>
#include <sys/types.h>
#include <stdexcept>
#include <errno.h>

#include "../threadpool/threadpool.h"

#define MAX_RESEVENT_SIZE 1024   // 事件的最大个数

class WebServer{
public:
    WebServer();
    
    // 创建套接字等待客户端连接，并开启监听
    int createListenFd(int port, const char* ip = nullptr );
    
    // 创建 epoll 例程用于监听套接字
    int createEpoll();
    
    // 向 epoll 中添加监控 Listen 套接字
    int epollAddListenFd();

    // 设置监听事件处理的管道
    int epollAddEventPipe();

    // 设置term和alarm信号的处理
    int addHandleSig(int signo = -1);

    // 信号处理函数
    static void setSigHandler(int signo);

    // 主线程中负责监听所有事件
    int waitEpoll();

    // 创建线程池
    int createThreadPool(int threadNum = 8);
    
    ~WebServer();
private:
    int m_listenfd;                   // 服务端的套接字
    sockaddr_in m_serverAddr;         // 服务端套接字绑定的地址信息
    static int m_epollfd;             // I/O 复用的 epoll 例程文件描述符
    static bool isStop;               // 是否暂停服务器

    static int eventHandlerPipe[2];   // 用于统一事件源传递信号的管道

    epoll_event resEvents[MAX_RESEVENT_SIZE]; // 保存 epoll_wait 结果的数组
    
    ThreadPool *threadPool;
};

#endif