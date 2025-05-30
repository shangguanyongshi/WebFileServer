//
// Created by wu on 25-5-26.
//

#include "utils.h"

std::string outHead(const std::string logType){
    auto now = std::chrono::system_clock::now();
    time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto time_tm = std::localtime(&nowTime);

    struct timeval time_usec;
    gettimeofday(&time_usec, NULL);

    char strTime[32] = { 0 };
    sprintf(strTime, "%02d:%02d:%02d.%05ld %d-%02d-%02d",
        time_tm->tm_hour, time_tm->tm_min, time_tm->tm_sec, time_usec.tv_usec,
        time_tm->tm_year + 1900, time_tm->tm_mon + 1, time_tm->tm_mday);

    std::string outStr;
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
    epoll_event event;
    event.data.fd = newFd;
    event.events = EPOLLIN;
    if (edgeTrigger){
        event.events |= EPOLLET;
    }
    // 如果设置了isOneshot标志，则将事件类型设置为EPOLLONESHOT
    if(isOneshot){
        event.events |= EPOLLONESHOT;
    }
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, newFd, &event);
    if(ret !=0){
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
