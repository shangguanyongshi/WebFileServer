#include "fileserver.h"

WebServer::WebServer(){

}
WebServer::~WebServer(){ 

}

// 创建套接字等待客户端连接，并开启监听
int WebServer::createListenFd(int port, const char* ip){
    // 指定地址
    bzero(&m_serverAddr, sizeof(m_serverAddr));
    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_port = htons(port);
    // 根据传入的 ip 确定是否指定 ip 地址
    if(ip != nullptr){
        m_serverAddr.sin_addr.s_addr = inet_addr(ip);
    }else{
        m_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    // 创建套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(m_listenfd < 0){
        std::cout << outHead("error") << "套接字创建失败" << std::endl;
        return -1;
    }

    int ret = 0;        // 保存函数执行的返回结果

    // 设置地址可重用
    int reuseAddr = 1;
    ret = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr));
    if(ret != 0){
        std::cout << outHead("error") << "套接字设置地址重用失败" << std::endl;
        return -2;
    }

    // 绑定地址信息
    ret = bind(m_listenfd, (sockaddr*)&m_serverAddr, sizeof(m_serverAddr));
    if(ret != 0){
        std::cout << outHead("error") << "套接字绑定地址失败" << std::endl;
        return -3;
    }

    // 开启监听
    ret = listen(m_listenfd, 5);
    if(ret != 0){
        std::cout << outHead("error") << "套接字开启监听失败" << std::endl;
        return -4;
    }

    return 0;
}

// 创建 epoll 例程用于监听套接字
int WebServer::createEpoll(){
    m_epollfd = epoll_create(100);
    if(m_epollfd < 0){
        std::cout << outHead("error") << "创建 epoll 失败" << std::endl;
        return -1;
    }
    return 0;
}

// 向 epoll 中添加监控 Listen 套接字
int WebServer::epollAddListenFd(){
    // ListenFd 设置为 边沿触发、非阻塞
    setNonBlocking(m_listenfd);
    // 因为需要将连接客户端的任务交给子线程处理，所以设置为边沿触发，避免子线程还没有接受连接时事件一直产生
    int ret = addWaitFd(m_epollfd, m_listenfd, true, false);
    if(ret != 0){
        std::cout << outHead("error") << "添加监控 Listen 套接字失败" << std::endl;
        return -1;
    }
    std::cout << outHead("info") << "epoll 中添加监听套接字成功" << std::endl;
    return 0;
}    

// 设置监听事件处理的管道
int WebServer::epollAddEventPipe(){
    int ret = socketpair(PF_UNIX, SOCK_STREAM, IPPROTO_TCP, eventHandlerPipe);
    if(ret != 0){
        std::cout << outHead("error") << "创建双向管道失败" << std::endl;
        return -1;
    }
    ret = setNonBlocking(eventHandlerPipe[0]);
    if(ret != 0){
        std::cout << outHead("error") << "设置 pipe[0] 非阻塞失败" << std::endl;
        return -2;
    }
    ret = setNonBlocking(eventHandlerPipe[1]);
    if(ret != 0){
        std::cout << outHead("error") << "设置 pipe[1] 非阻塞失败" << std::endl;
        return -3;
    }
    ret = addWaitFd(m_epollfd, eventHandlerPipe[0]);
    if(ret != 0){
        std::cout << outHead("error") << "添加监控 pipe[0] 失败" << std::endl;
        return -4;
    }
    return 0;
}


// 设置term和alarm信号的处理
int WebServer::addHandleSig(int signo){
    int ret = 0;
    // 当参数 signo 为 -1 时，表示添加对默认信号的处理
    if(signo == -1){
        // 处理 SIGINT 信号
        struct sigaction actINT;
        actINT.sa_handler = setSigHandler;
        sigfillset(&actINT.sa_mask);
        actINT.sa_flags = 0;
        ret = sigaction(SIGINT, &actINT, nullptr);
        if(ret != 0){
            std::cout << outHead("error") << "SIGINT 指定信号处理函数失败" << std::endl;
            return -1;
        }
        // 处理 SIGTERM 信号
        struct sigaction actTERM;
        actTERM.sa_handler = setSigHandler;
        sigfillset(&actTERM.sa_mask);
        actTERM.sa_flags = 0;
        ret = sigaction(SIGTERM, &actTERM, nullptr);
        if(ret != 0){
            std::cout << outHead("error") << "SIGTERM 指定信号处理函数失败" << std::endl;
            return -1;
        }
        // 处理 SIGALRM 信号
        struct sigaction actALRM;
        actALRM.sa_handler = setSigHandler;
        sigfillset(&actALRM.sa_mask);
        actALRM.sa_flags = 0;
        ret = sigaction(SIGALRM, &actALRM, nullptr);
        if(ret != 0){
            std::cout << outHead("error") << "SIGALRM 指定信号处理函数失败" << std::endl;
            return -1;
        }
        return 0;
    }

    // 参数 signo 不为 -1 时，表示添加监听的信号
    struct sigaction act;
    act.sa_handler = setSigHandler;
    sigfillset(&act.sa_mask);
    act.sa_flags = 0;
    ret = sigaction(signo, &act, nullptr);
    if(ret != 0){
        std::cout << outHead("error") << "指定信号处理函数失败" << std::endl;
        return -1;
    }
    return 0;
}


// 信号处理函数
void WebServer::setSigHandler(int signo){
    if((signo & SIGINT) | (signo & SIGTERM)){
        isStop = true;
        return;
    }
    int saveErrno = errno;
    int msg = signo;
    int ret = send(eventHandlerPipe[1], &msg, 1, 0);
    if(ret != 0){
        std::cout << outHead("error") << "信号处理失败" << std::endl;
    }
    errno = saveErrno;
}

// 主线程中负责监听所有事件
int WebServer::waitEpoll(){
    // 标识服务器是否暂停
    isStop = false;

    // 创建事件保存事件的临时指针
    EventBase *event = nullptr;

    while(!isStop){
        int resNum = epoll_wait(m_epollfd, resEvents, MAX_RESEVENT_SIZE, -1);
        // 如果 epoll_wait 执行出错，直接退出（因为事件发生导致返回 -1 时，errno会置 ENITR，需要在事件处理函数中保留 errno）
        if(resNum < 0 && errno != EINTR ){
            std::cout << outHead("error") << "epoll_wait 执行错误" << std::endl;
            return -1;
        }
        std::string eventType;
        for(int i = 0; i < resNum; ++i){
            int resfd = resEvents[i].data.fd;
            if(resfd == m_listenfd){
                std::cout << outHead("info") << "有新的连接请求" << std::endl;
                // 构建接受连接的事件
                event = new AcceptConn(m_listenfd, m_epollfd);
                eventType = "新连接事件";
            }else if((resfd == eventHandlerPipe[0]) && (resEvents[i].events & EPOLLIN)){
                // 如果有事件发生，执行事件处理函数
                
                eventType = "新信号事件";
            }else if(resEvents[i].events & EPOLLIN){
                // 构建读取客户端数据的事件
                event = new HandleRecv(resEvents[i].data.fd, m_epollfd);
                eventType = "新可读事件";

            }else if(resEvents[i].events & EPOLLOUT){
                // 套接字可以发送数据，构建可以发送数据的事件
                event = new HandleSend(resEvents[i].data.fd, m_epollfd);
                eventType = "新可写事件";
            }
            if(event == nullptr){
                continue;
            }
            // 将事件加入线程池的待处理队列。在线程池中，事件执行完后销毁事件，可以修改为智能指针自动释放
            threadPool->appendEvent(event, eventType);
            
            // 将 event 置空
            event = nullptr;
        }
    }
    return 0;
}

// 创建线程池
int WebServer::createThreadPool(int threadNum){
    try{
        threadPool = new ThreadPool(threadNum);
    }catch(std::runtime_error &err){
        std::cout << err.what() << std::endl;
    }
    if(threadPool == nullptr){
        std::cout << outHead("error") << "线程池创建失败" << std::endl;
        return -1;
    }
    return 0;
}




int WebServer::m_epollfd = -1;
bool WebServer::isStop = false;
int WebServer::eventHandlerPipe[2] = {-1, -1};
