/*  文件说明：
 *  1. 该文件中定义的类用于在事件处理中表示 “请求消息” 和 “响应消息”
 *  2. Message 是基类，用来保存两种消息共有的信息：消息处理的状态和消息首部的信息
 *  3. Request 表示浏览器的请求消息，用来记录其中的重要字段，及事件处理中对请求消息处理了多少
 *  4. Response 表示向客户端回复的响应消息，记录响应消息中待发送的数据，如状态行、首部、消息体、响应消息已发送多少
 */

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

// 定义 Request 和 Response 公共的部分，即消息首部、消息体（可以获取消息首部的某个字段、修改与获取消息体相关的数据）
class Message{
public:
    Message() : status(HANDLE_INIT){  // 默认构造函数，将状态设置为初始值(正在接收/发送头部数据（请求行、请求头）)

    }

public:
    // 请求消息和响应消息都需要使用的一些成员
    MSGSTATUS status;                                        // 记录消息的接收状态，表示整个请求报文收到了多少/发送了多少

    std::unordered_map<std::string, std::string> msgHeader;  // 保存消息首部，存储的是键值对（key-value pairs）首部字段 map（如 Content-Type, Content-Length）

private:
    
};
//请求消息
//继承自 Message，代表从浏览器接收到的 HTTP 请求：
// 继承 Message，对请求行的修改和获取，保存收到的首部选项
class Request : public Message{
public:
    Request() : Message(){

    }
    // 设置与返回请求行相关字段
    /**
     * @brief 设置请求行
     *
     * 从传入的字符串中提取请求方法、请求资源和HTTP版本，并保存到类中相应的成员变量中。
     * 比如 GET /index.html HTTP/1.1
     * @param requestLine 请求行字符串
     */
    void setRequestLine(const std::string &requestLine){
        std::istringstream lineStream(requestLine);
        // 获取请求方法
        lineStream >> requestMethod;   // GET/POST等
        // 获取请求资源
        // lineStream >> rquestResourse;
        lineStream >> requestResourse;  // 请求的资源路径

        // 获取http版本
        lineStream >> httpVersion;  // 协议版本
        
    }

    // 对于Request 报文，根据传入的一行首部字符串，向首部保存选项
    /**
     * @brief 添加头部选项
     *
     * 该函数将传入的头部选项解析并存入 msgHeader 中。
     * 比如Content-Type: multipart/form-data; boundary=xxx
     * @param headLine 要解析的头部选项字符串
     */
    void addHeaderOpt(const std::string &headLine){
        static std::istringstream lineStream;
        lineStream.str(headLine);    // 以 istringstream 的方式处理头部选项

        std::string key, value;      // 保存键和值的临时量

        lineStream >> key;           // 获取 key
        key.pop_back();              // 删除键中的冒号 
        lineStream.get();            // 删除冒号后的空格

        // 读取空格之后所有的数据，遇到 \n 停止，所以 value 中还包含一个 \r
        getline(lineStream, value);
        value.pop_back();            // 删除其中的 \r
        
        if(key == "Content-Length"){
            // 保存消息体的长度
            contentLength = std::stoll(value);

        }else if(key == "Content-Type"){
            // 分离消息体类型。消息体类型可能是复杂的消息体，类似 Content-Type: multipart/form-data; boundary=---------------------------24436669372671144761803083960
            
            // 先找出值中分号的位置
            std::string::size_type semIndex = value.find(';');
            // 根据分号查找的结果，保存类型的结果
            if(semIndex != std::string::npos){
                msgHeader[key] = value.substr(0, semIndex);
                std::string::size_type eqIndex = value.find('=', semIndex);
                key = value.substr(semIndex + 2, eqIndex - semIndex - 2);
                msgHeader[key] = value.substr(eqIndex + 1);
            }else{
                msgHeader[key] = value;
            }
            
        }else{
            msgHeader[key] = value;
        }

        
    }

public:
    std::string recvMsg;           // 收到但是还未处理的数据


    std::string requestMethod;     // 请求消息的请求方法
    std::string requestResourse;    // 请求的资源
    std::string httpVersion;       // 请求的HTTP版本

    long long contentLength = 0;                 // 记录消息体的长度
    long long msgBodyRecvLen;                    // 已经接收的消息体长度

    std::string recvFileName;                    // 如果客户端发送的是文件，记录文件的名字
    FILEMSGBODYSTATUS fileMsgStatus;             // 记录表示文件的消息体已经处理到哪些部分
private:

};
// 响应消息
// 继承 Message，对于状态行修改和获取，设置要发送的首部选项
class Response : public Message{
public:
    Response() : Message(){

    }

public:
    // 保存状态行相关数据
    std::string responseHttpVersion = "HTTP/1.1";
    std::string responseStatusCode;  // 如 200、404
    std::string responseStatusDes;   // 如 OK、Not Found

    // 以下成员主要用于在发送响应消息时暂存相关的数据

    MSGBODYTYPE bodyType;                                 // 消息的类型
    std::string bodyFileName;                             // 要发送数据的路径


    std::string beforeBodyMsg;                            // 消息体之前的所有数据
    int beforeBodyMsgLen;                                 // 消息体之前的所有数据的长度

    std::string msgBody;                                  // 在字符串中保存 HTML 类型的消息体
    unsigned long msgBodyLen;                             // 消息体的长度

    int fileMsgFd;                                        // 文件类型的消息体保存文件描述符

    unsigned long curStatusHasSendLen;                    // 记录在当前状态下，这些数据已经发送的长度
private:
    
};


#endif