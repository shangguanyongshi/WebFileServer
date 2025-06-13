//
// Created by wu on 25-5-20.
//

#ifndef MESSAGE_H
#define MESSAGE_H
#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <unordered_map>

// 表示 Request 或 Response 中数据的处理状态
enum MSGSTATUS{
    HANDLE_INIT,      // 正在接收/发送头部数据（请求行、请求头）
    HANDLE_HEAD,      // 正在接收/发送消息首部
    HANDLE_BODY,      // 正在接收/发送消息体
    HADNLE_COMPLATE,  // 所有数据都已经处理完成
    HANDLE_ERROR,     // 处理过程中发生错误
};

// 表示消息体的类型
enum MSGBODYTYPE{
    FILE_TYPE,      // 消息体是文件
    HTML_TYPE,      // 消息体是 HTML 页面
    EMPTY_TYPE,     // 消息体为空
};

// 当接收文件时，消息体会分不同的部分，用该类型表示文件消息体已经处理到哪个部分
enum FILEMSGBODYSTATUS{
    FILE_BEGIN_FLAG,   // 正在获取并处理表示文件开始的标志行
    FILE_HEAD,         // 正在获取并处理文件属性部分
    FILE_CONTENT,      // 正在获取并处理文件内容的部分
    FILE_COMPLATE      // 文件已经处理完成
};

class Message{
public:
    MSGSTATUS status;
    std::unordered_map<std::string, std::string> msgHeader;
    Message():status(HANDLE_INIT){

    }
};
/*
GET /index.html HTTP/1.1
Host: example.com
User-Agent: curl/7.68.0
Accept: xxx
*/

class Request : public Message{
public:
    Request():Message(){}
    void setRequestLine(const std::string &requestLine){
        std::istringstream lineStream(requestLine);
        // GET /index.html HTTP/1.1
        lineStream >> requestMethod;
        lineStream >> requestResourse;
        lineStream >> httpVersion;
    }
    /**
     * @brief 向消息头添加选项
     *
     * 将给定的消息头字符串解析并存储到全局的消息头map中。如果消息头包含特殊字段（如Content-Length、Content-Type），
     * 则进行特殊处理。
     *
     * @param headLine 要添加的消息头字符串
     */
    void addHeaderOpt(const std::string &headLine){
        static std::istringstream lineStream;
        lineStream.str(headLine);

        std::string key, value;
        lineStream >> key;
        key.pop_back();     // 删除键中的冒号
        lineStream.get();   // 删除冒号后的空格

        // 你读取的行是以 \r\n 结尾的（标准 HTTP 报文是这样的）。
        // 读取空格之后所有的数据，遇到 \n 停止，所以 value 中还包含一个 \r
        std::getline(lineStream, value);
        value.pop_back(); // 删除其中的 \r
        if (key == "Content-Length"){
            contentLength = std::stoll(value); // 将字符串转化为长整型
        }else if(key == "Content-Type"){
            //消息体类型可能是复杂的消息体，类似 Content-Type: multipart/form-data; boundary=---------------------------24436669372671144761803083960
            // 先找出值中分号的位置
            std::string::size_type semIndex = value.find(';');
            if (semIndex != std::string::npos){ // 找到了
                msgHeader[key] = value.substr(0, semIndex);
                std::string::size_type eqIndex = value.find('=', semIndex);
                key = value.substr(semIndex+2, eqIndex-semIndex-2);//
                msgHeader[key] = value.substr(eqIndex + 1);
            }else{//没有分号
                msgHeader[key] = value;
            }
        }else{
            msgHeader[key] = value;
        }
    }
public:
    std::string recvMsg;
    std::string requestMethod;
    std::string requestResourse;
    std::string httpVersion;

    long long contentLength=0;
    long long msgBodyRecvLen;  // 已经接收的消息体长度

    std::string recvFileName;          // 如果客户端发送的是文件，记录文件的名字
    FILEMSGBODYSTATUS fileMsgStatus;   // 记录表示文件的消息体已经处理到哪些部分


};
class Response : public Message{
public:
    Response(): Message(){}

public:
    std::string responseHttpVersion = "HTTP/1.1";
    std::string responseStatusCode;
    std::string responseStatusDes;

    MSGBODYSTATUS bodyType;    // 消息的类型
    std::string bodyFileName;   // 要发送数据的路径

    std::string beforeBodyMsg;
    int beforeBodyMsgLen;

    std::string msgBody;
    unsigned int msgBodyLen;

    int fileMsgFd;

    unsigned long curStatusHasSendLen;  // 记录在当前状态下，这些数据已经发送的长度

};

#endif //MESSAGE_H
