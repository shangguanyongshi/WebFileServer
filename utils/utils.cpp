#include "utils.h"

// 以 "09:50:19.0619 2022-09-26 [logType]: " 格式返回当前的时间和输出类型，logType 指定输出的类型：
// init  : 表示服务器的初始化过程
// error : 表示服务器运行中的出错消息
// info  : 表示程序的运行信息
std::string outHead(const std::string logType){
    // 获取并输出时间
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    auto time_tm = localtime(&tt);
    
    struct timeval time_usec;
    gettimeofday(&time_usec, NULL);

    char strTime[30] = { 0 };
    //16:31:15.695681 2025-05-10
    sprintf(strTime, "%02d:%02d:%02d.%05ld %d-%02d-%02d",
            time_tm->tm_hour, time_tm->tm_min, time_tm->tm_sec, time_usec.tv_usec,
            time_tm->tm_year + 1900, time_tm->tm_mon + 1, time_tm->tm_mday);

    std::string outStr;
    // 添加时间部分
    outStr += strTime;
    // 根据传入的参数指定输出的类型
    if(logType == "init"){
        outStr += " [init]: ";
    }else if(logType == "error"){
        outStr += " [erro]: ";
    }else{
        outStr += " [info]: ";
    }
    //得到16:31:15.695681 2025-05-10 [info]:
    return outStr;
}


// 向 epollfd 添加文件描述符newFd，并指定监听事件。edgeTrigger：边缘触发，isOneshot：EPOLLONESHOT
int addWaitFd(int epollFd, int newFd, bool edgeTrigger, bool isOneshot){
    // 创建一个epoll事件对象
    epoll_event event;
    // 设置事件对象中的文件描述符
    event.data.fd = newFd;

    // 初始化事件类型为EPOLLIN
    event.events = EPOLLIN;
    // 如果设置了edgeTrigger标志，则将事件类型设置为EPOLLET
    if(edgeTrigger){
        event.events |= EPOLLET;
    }
    // 如果设置了isOneshot标志，则将事件类型设置为EPOLLONESHOT
    if(isOneshot){
        event.events |= EPOLLONESHOT;
    }

    // 将新文件描述符添加到epoll实例中
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, newFd, &event);
    // 如果添加失败，则输出错误信息并返回-1
    if(ret != 0){
        std::cout << outHead("error") << "添加文件描述符失败" << std::endl;
        return -1;
    }
    // 添加成功，返回0
    return 0;
}

// 修改正在监听文件描述符的事件。edgeTrigger:是否为边沿触发，resetOneshot:是否设置 EPOLLONESHOT，addEpollout:是否监听输出事件
int modifyWaitFd(int epollFd, int modFd, bool edgeTrigger, bool resetOneshot, bool addEpollout){
    epoll_event event;
    event.data.fd = modFd;

    event.events = EPOLLIN;

    if(edgeTrigger){
        event.events |= EPOLLET;
    }
    if(resetOneshot){
        event.events |= EPOLLONESHOT;
    }
    if(addEpollout){
        event.events |= EPOLLOUT;
    }

    int ret = epoll_ctl(epollFd, EPOLL_CTL_MOD, modFd, &event);
    if(ret != 0){
        std::cout << outHead("error") << "修改文件描述符失败" << std::endl;
        return -1;
    }
    return 0;
}

// 删除正在监听的文件描述符
int deleteWaitFd(int epollFd, int deleteFd){
    int ret = epoll_ctl(epollFd, EPOLL_CTL_DEL, deleteFd, nullptr);
    if(ret != 0){
        std::cout << outHead("error") << "删除监听的文件描述符失败" << std::endl;
        return -1;
    }
    return 0;
}

// 设置文件描述符为非阻塞

int setNonBlocking(int fd){
    // 获取文件描述符的当前标志
    int oldFlag = fcntl(fd, F_GETFL);
    // 设置文件描述符为非阻塞模式
    int ret = fcntl(fd, F_SETFL, oldFlag | O_NONBLOCK);
    // 检查设置是否成功
    if(ret != 0){
        // 设置失败，返回-1
        return -1;
    }
    // 设置成功，返回0
    return 0;
}
