//
// Created by wu on 25-5-26.
//

#ifndef MYEVENT_H
#define MYEVENT_H

#include <dirent.h>
#include <fstream>
#include <vector>
#include <cstdio>

#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include "../message/message.h"
#include "../utils/utils.h"
class EventBase{
public:
    EventBase(){}
    virtual ~EventBase(){}
protected:
    static std::unordered_map<int, Request> requestStatus;
    static std::unordered_map<int, Response> responseStatus;

public:
    virtual void process(){}

};
// 用于接受客户端连接的事件
class AcceptConn : public EventBase{
public:
    AcceptConn(int listenFd, int epollFd): m_listenFd(listenFd), m_epollFd(epollFd){}
    virtual ~AcceptConn(){}

    virtual void process() override;

private:
    int m_listenFd;
    int m_epollFd;
    int acceptFd;
    sockaddr_in clientAddr;
    socklen_t clientAddrLen;
};

// 处理客户端发送的请求，也就是接收到的请求
class HandleRecv : public EventBase{
public:
    HandleRecv(int clientFd, int epollFd) : m_clientFd(clientFd), m_epollFd(epollFd){ };
    virtual ~HandleRecv(){ };
public:
    virtual void process() override;

private:
    int m_clientFd;   // 客户端套接字，从该客户端读取数据
    int m_epollFd;    // epoll 文件描述符，在需要重置事件或关闭连接时使用
};
// 处理向客户端发送数据
class HandleSend: public EventBase{
public:
    HandleSend(int clientFd, int epollFd): m_clientFd(clientFd), m_epollFd(epollFd){ };
    virtual ~HandleSend(){ };

public:
    virtual void process() override;
    // 用于构建状态行，参数分别表示状态行的三个部分
    std::string getStatusLine(const std::string &httpVersion, const std::string &statusCode, const std::string &statusDes);
    // 以下两个函数用来构建文件列表的页面，最终结果保存到 fileListHtml 中
    void getFileListPage(std::string &fileListHtml);
    void getFileVec(const std::string dirName, std::vector<std::string> &resVec);

    // 构建头部字段：
    // contentLength        : 指定消息体的长度
    // contentType          : 指定消息体的类型
    // redirectLoction = "" : 如果是重定向报文，可以指定重定向的地址。空字符串表示不添加该首部。
    // contentRange = ""    : 如果是下载文件的响应报文，指定当前发送的文件范围。空字符串表示不添加该首部。
    std::string getMessageHeader(const std::string contentLength, const std::string contentType,
                                 const std::string redirectLoction = "", const std::string contentRange = "");

private:
    int m_clientFd; // 客户端套接字，向该客户端写数据
    int m_epollFd;  // epoll 文件描述符，在需要重置事件或关闭连接时使用

};

#endif //MYEVENT_H
