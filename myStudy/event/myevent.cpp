//
// Created by wu on 25-5-26.
//

#include "myevent.h"

// 类外初始化静态成员
std::unordered_map<int, Request> EventBase::requestStatus;
std::unordered_map<int, Response> EventBase::responseStatus;
//接收客户端连接的事件
void AcceptConn::process(){
    clientAddrLen = sizeof(clientAddr);
    int accFd = accept(m_listenFd, (sockaddr*)&clientAddr, &clientAddrLen);
    if(accFd == -1){
        std::cout << outHead("error") << "接受新连接失败" << std::endl;
        return;
    }
    // 将连接设置为非阻塞
    setNonBlocking(accFd);
    // 将连接加入到监听，客户端套接字都设置为 EPOLLET 和 EPOLLONESHOT
    addWaitFd(m_epollFd, accFd, true, true);
    std::cout << outHead("info") << "接受新连接 " << accFd << " 成功" << std::endl;

}
//处理客户端发送的请求，也就是接收到的请求
void HandleRecv::process(){
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleRecv 事件" << std::endl;
    requestStatus[m_clientFd];  //先创建一个

    char buf[2048];
    int recvLen = 0;
    while(1){
        // 循环接收数据，直到缓冲区读取不到数据或请求消息处理完成时退出循环
        recvLen = recv(m_clientFd, buf, sizeof(buf), 0);
        if(recvLen == 0){
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 关闭连接" << std::endl;
            requestStatus[m_clientFd].status = HANDLE_ERROR;
            break;
        }
        //如果缓冲区的数据已经读完，退出读数据的状态
        if(recvLen == -1){
            if(errno != EAGAIN){    // 如果不是缓冲区为空，设置状态为错误，并退出循环
                requestStatus[m_clientFd].status = HANDLE_ERROR;
                std::cout << outHead("error") << "接收数据时返回 -1 (errno = " << errno << ")" << std::endl;
                break;
            }
            // 如果是缓冲区为空，表示需要等待数据发送，由于是 EPOLLONESHOT，再退出循环，等再发来数据时再来处理
            modifyWaitFd(m_epollFd, m_clientFd, true, true, false);
            break;
        }
        // 将收到的数据拼接到之前收到的数据后面，由于在处理文件时，里面可能有 \0，所以使用 append 将 buf 内的所有字符都保存到 recvMsg 中
        requestStatus[m_clientFd].recvMsg.append(buf, recvLen);
        // 边接收数据边处理
        // 根据请求报文的状态执行操作，以下操作中，如果成功了，则解析请求报文的下个部分，如果某个部分还没有完全接收，会退出当前处理步骤，等再次收到数据后根据这次解析的状态继续处理
        // 保存字符串查找结果，每次查找都可以用该变量暂存查找结果
        std::string::size_type endIndex = 0;
        // 如果是初始状态，获取请求行
        if(requestStatus[m_clientFd].status == HANDLE_INIT){
            endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");
            if(endIndex != std::string::npos){
                // 读取 GET /index.html HTTP/1.1
                requestStatus[m_clientFd].setRequestLine(requestStatus[m_clientFd].recvMsg.substr(0, endIndex + 2));
                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);
                requestStatus[m_clientFd].status = HANDLE_HEAD; // 将状态设置为处理消息首部
                std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的请求行完成" << std::endl;
            }
            // 如果没有找到 \r\n，表示数据还没有接收完成，会跳回上面继续接收数据
        }
        // 如果是处理首部的状态，逐行解析首部字段，直至遇到空行
        if(requestStatus[m_clientFd].status == HANDLE_HEAD){
            std::string curLine;       // 用于暂存获取的一行数据
            while(1){
                endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");            // 获取一行的边界
                if(endIndex == std::string::npos){                                    // 如果没有找到边界，表示后面的数据还没有接收完整，退出循环，等待下次接收后处理
                    break;
                }
                curLine = requestStatus[m_clientFd].recvMsg.substr(0, endIndex+2);
                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);

                //如果只是\r\n,说明下一行就到文件内容 了
                if(curLine == "\r\n"){
                    requestStatus[m_clientFd].status = HANDLE_BODY;
                    if(requestStatus[m_clientFd].msgHeader["Content-Type"] == "multipart/form-data"){     // 如果接收的是文件，设置消息体中文件的处理状态
                        requestStatus[m_clientFd].fileMsgStatus = FILE_BEGIN_FLAG;
                    }
                    std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的消息首部完成" << std::endl;
                    if(requestStatus[m_clientFd].requestMethod == "POST"){
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 POST 请求，开始处理请求体" << std::endl;
                    }
                    //需要退出处理首部
                    break;
                }

                requestStatus[m_clientFd].addHeaderOpt(curLine);     // 如果不是空行，需要将该首部保存
            }
        }
        if(requestStatus[m_clientFd].status == HANDLE_BODY){
            if(requestStatus[m_clientFd].requestMethod == "GET"){
                // 设置响应消息的资源路径，在 HandleSend 中根据请求资源构建整个响应消息并发送
                responseStatus[m_clientFd].bodyFileName = requestStatus[m_clientFd].rquestResourse;
                // 设置监听套接字的可写事件，当套接字写缓冲区有空闲数据时，会产生 HandleSend 事件，将 m_clientFd 索引的 responseStatus 中的数据发送
                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                requestStatus[m_clientFd].status = HADNLE_COMPLATE;
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 GET 请求，已将请求资源构成 Response 写事件等待发送数据" << std::endl;
                break;
            }
            // POST 表示上传数据，执行接收数据的操作
            if(requestStatus[m_clientFd].requestMethod == "POST"){
                if(requestStatus[m_clientFd].msgHeader["Content-Type"] =="multipart/form-data"){
                    if(requestStatus[m_clientFd].fileMsgStatus == FILE_BEGIN_FLAG){

                	}
                }

            }
        }
    }


}