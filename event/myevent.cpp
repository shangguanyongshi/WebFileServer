#include <string>
#include <sstream>
#include <iomanip>
#include "myevent.h"

// 类外初始化静态成员
std::unordered_map<int, Request> EventBase::requestStatus;
std::unordered_map<int, Response> EventBase::responseStatus;


std::string urlDecode(const std::string& encoded) {
    std::string decoded;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            // 解析%XX形式的编码
            int value;
            std::istringstream iss(encoded.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                decoded += static_cast<char>(value);
                i += 2; // 跳过已处理的两位
            } else {
                decoded += encoded[i]; // 无效编码，保留原字符
            }
        } else if (encoded[i] == '+') {
            decoded += ' '; // URL中+表示空格
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

// 用于接受客户端连接的事件
void AcceptConn::process(){
    // 接受连接
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

// 处理客户端发送的请求
void HandleRecv::process(){
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleRecv 事件" << std::endl;
    // 获取 Request 对象，保存到m_clientFd索引的requestStatus中（没有时会自动创建一个新的）
    requestStatus[m_clientFd];

    // 读取输入，检测是否是断开连接，否则处理请求
    char buf[2048];
    int recvLen = 0;
    
    while(1){
        // 循环接收数据，直到缓冲区读取不到数据或请求消息处理完成时退出循环
        recvLen = recv(m_clientFd, buf, 2048, 0);

        // 对方关闭连接，直接断开连接，设置当前状态为 HANDLE_ERROR，再退出循环
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
        // POST /upload HTTP/1.1\r\n,setRequestLine 会解析出 requestMethod="POST"，requestResourse="/upload"，httpVersion="HTTP/1.1"。
        if(requestStatus[m_clientFd].status == HANDLE_INIT){

            endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");       // 查找请求行的结束边界

            if(endIndex != std::string::npos){
                // 保存请求行  
                requestStatus[m_clientFd].setRequestLine(requestStatus[m_clientFd].recvMsg.substr(0, endIndex + 2) ); // std::cout << requestStatus[m_clientFd].recvMsg.substr(0, endIndex + 2);
                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);    // 删除收到的数据中的请求行
                requestStatus[m_clientFd].status = HANDLE_HEAD;              // 将状态设置为处理消息首部
                std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的请求行完成" << std::endl;
            }

            // 如果没有找到 \r\n，表示数据还没有接收完成，会跳回上面继续接收数据
        }
        
        // 如果是处理首部的状态，逐行解析首部字段，直至遇到空行
        //请求头可能包含 Content-Type: multipart/form-data; boundary=----WebKitFormBoundaryxxx，addHeaderOpt 会解析出 Content-Type 和 boundary（用于后续文件边界判断）。
        if(requestStatus[m_clientFd].status == HANDLE_HEAD){
            
            std::string curLine;       // 用于暂存获取的一行数据

            while(1){
                
                endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");            // 获取一行的边界
                if(endIndex == std::string::npos){                                    // 如果没有找到边界，表示后面的数据还没有接收完整，退出循环，等待下次接收后处理
                    break;
                }

                curLine = requestStatus[m_clientFd].recvMsg.substr(0, endIndex + 2);  // 将该行的内容取出
                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);             // 删除收到的数据中的该行数据

                if(curLine == "\r\n"){
                    requestStatus[m_clientFd].status = HANDLE_BODY;                                       // 如果是空行，将状态修改为等待解析消息体
                    if(requestStatus[m_clientFd].msgHeader["Content-Type"] == "multipart/form-data"){     // 如果接收的是文件，设置消息体中文件的处理状态
                        requestStatus[m_clientFd].fileMsgStatus = FILE_BEGIN_FLAG;
                    }
                    std::cout << outHead("info") << "处理客户端 " << m_clientFd << " 的消息首部完成" << std::endl;
                    if(requestStatus[m_clientFd].requestMethod == "POST"){
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 POST 请求，开始处理请求体" << std::endl;
                    }
                    break;                                                                                // 退出首部字段循环
                }
                
                requestStatus[m_clientFd].addHeaderOpt(curLine);                      // 如果不是空行，需要将该首部保存
            }
        }

        // 如果是处理消息体的状态，根据请求类型执行特定的操作
        if(requestStatus[m_clientFd].status == HANDLE_BODY){
            // GET 操作时表示请求数据，将请求的资源路径交给 HandleSend 事件处理
            if(requestStatus[m_clientFd].requestMethod == "GET"){
                // 设置响应消息的资源路径，在 HandleSend 中根据请求资源构建整个响应消息并发送
                responseStatus[m_clientFd].bodyFileName = requestStatus[m_clientFd].requestResourse;

                // 设置监听套接字的可写事件，当套接字写缓冲区有空闲数据时，会产生 HandleSend 事件，将 m_clientFd 索引的 responseStatus 中的数据发送
                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                requestStatus[m_clientFd].status = HADNLE_COMPLATE;
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 发送 GET 请求，已将请求资源构成 Response 写事件等待发送数据" << std::endl; 
                break;
            }

            // POST 表示上传数据，执行接收数据的操作
            if(requestStatus[m_clientFd].requestMethod == "POST"){
                // 记录未处理的数据长度，用于当前 if 步骤处理结束时，计算处理了多少消息体数据，处理非文件时用来判断数据边界（文件使用 boundary 确定边界）
                std::string::size_type beginSize = requestStatus[m_clientFd].recvMsg.size();
                if(requestStatus[m_clientFd].msgHeader["Content-Type"] == "multipart/form-data"){  // 如果发送的是文件
                    // 如果处于等待处理文件开始标志的状态，查找 \r\n 判断标志部分是否已经接收
                    if(requestStatus[m_clientFd].fileMsgStatus == FILE_BEGIN_FLAG){
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求用于上传文件，寻找文件头开始边界..." << std::endl;
                        // 查找 \r\n
                        endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");

                        // 当前状态下，\r\n 前的数据必然是文件信息开始的标志
                        if(endIndex != std::string::npos){
                            std::string flagStr = requestStatus[m_clientFd].recvMsg.substr(0, endIndex);

                            if(flagStr == "--" +requestStatus[m_clientFd].msgHeader["boundary"]){  // 如果等于 "--" 加边界，进入下一个状态
                                requestStatus[m_clientFd].fileMsgStatus = FILE_HEAD;               // 进入下一个状态
                                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);          // 将开始标志行删除（包括 /r/n）
                                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中找到文件头开始边界，正在处理文件头..." << std::endl;
                            }else{
                                // 如果和边界不同，表示出错，直接返回重定向报文，重新请求文件列表
                                responseStatus[m_clientFd].bodyFileName = "/redirect"; 
                                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);   // 重置可读事件和可写事件，用于发送重定向回复报文
                                requestStatus[m_clientFd].status = HADNLE_COMPLATE;
                                std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求体中没有找到文件头开始边界，添加重定向 Response 写事件，使客户端重定向到文件列表" << std::endl;
                                break;
                            }
                        }
                    }

                    // 如果处于等待接收并处理消息体中文件头部信息的状态，从中提取文件名
                    if(requestStatus[m_clientFd].fileMsgStatus == FILE_HEAD){
                        std::string strLine;
                        while(1){
                            // 查找 \r\n 表示一行数据
                            endIndex = requestStatus[m_clientFd].recvMsg.find("\r\n");
                            if(endIndex != std::string::npos){
                                strLine = requestStatus[m_clientFd].recvMsg.substr(0, endIndex + 2);  // 获取这一行的数据信息
                                requestStatus[m_clientFd].recvMsg.erase(0, endIndex + 2);             // 删除这一行信息

                                // 检测是否为空行，如果是空行，修改状态，退出
                                if(strLine == "\r\n"){
                                    requestStatus[m_clientFd].fileMsgStatus = FILE_CONTENT;
                                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中文件头处理成功，正在接收并保存文件内容..." << std::endl;
                                    break;
                                }
                                // 查找 strLine 是否包含 filename
                                endIndex = strLine.find("filename");
                                if(endIndex != std::string::npos){
                                    strLine.erase(0, endIndex + std::string("filename=\"").size());          // 将真正 filename 前的所有字符删除
                                    for(int i = 0; strLine[i] != '\"'; ++i){                                 // 保存文件名
                                        requestStatus[m_clientFd].recvFileName += strLine[i];
                                    }
                                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中找到文件名字 " << requestStatus[m_clientFd].recvFileName << " ，继续处理文件头..." << std::endl; 
                                }
                            }else{   // 如果没有找到，表示消息还没有接收完整，退出，等待下一轮的事件中继续处理
                                break;
                            }
                        }
                    }

                    // 如果处于等待并处消息体中文件内容部分
                    // 循环检索是否有 \r\n ，将 \r\n 之前的内容全部保存。如果存在\r\n，根据后面的内容判断是否到达文件边界
                    if(requestStatus[m_clientFd].fileMsgStatus == FILE_CONTENT){
                        // 首先以二进制追加的方式打开文件
                        std::ofstream ofs("filedir/" + requestStatus[m_clientFd].recvFileName, std::ios::out | std::ios::app | std::ios::binary);
                        if(!ofs){
                            std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求体所需要保存的文件打开失败，正在重新打开文件..." << std::endl;
                            break;
                        }

                        while(1){
                            int saveLen = requestStatus[m_clientFd].recvMsg.size();        // 该变量用来保存 根据\r的位置决定向文件中写入多少字符，初始为所有字符长度
                            if(saveLen == 0){                                              // 长度为空时退出循环，等待接收到数据时再处理
                                break;
                            }
                            // 在剩余的字符中搜索标志 \r
                            endIndex = requestStatus[m_clientFd].recvMsg.find('\r');
                                        
                            if(endIndex != std::string::npos){   // 如果有\r，后面有可能是文件结束标识
                                // 首先判断 \r 后的数据是否满足结束标识的长度，是否大于等于 sizeof(\r\n + "--" + boundary + "--" + \r\n)
                                int boundarySecLen = requestStatus[m_clientFd].msgHeader["boundary"].size() + 8;
                                if(requestStatus[m_clientFd].recvMsg.size() - endIndex >= boundarySecLen){
                                    // 判断后面这部分数据是否为结束边界"\r\n"
                                    if(requestStatus[m_clientFd].recvMsg.substr(endIndex, boundarySecLen) ==
                                                    "\r\n--" + requestStatus[m_clientFd].msgHeader["boundary"] + "--\r\n"){
                                        if(endIndex == 0){                  // 表示边界前的数据都已经写入文件，设置文件接收完成，进入下一个状态
                                            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体中的文件数据接收并保存完成" << std::endl;
                                            requestStatus[m_clientFd].fileMsgStatus = FILE_COMPLATE;
                                            break;
                                        }

                                        // 如果后面不是结束标识，先将 \r 之前的所有数据写入文件，在循环的下一轮会进到上一个if，结束整个处理
                                        saveLen = endIndex;
                                        
                                    }else{  
                                        // 如果不是边界，在 \r 后再次搜索 \r，如果搜索到了，写入的数据截至到第二个 \r，否则将所有数据写入
                                        endIndex = requestStatus[m_clientFd].recvMsg.find('\r', endIndex + 1);
                                        if(endIndex != std::string::npos){
                                            saveLen = endIndex;
                                        }
                                    }

                                }else{  
                                    // 如果长度还不够，将 /r 前面的数据写入文件，并等待接收后面的数据

                                    // 如果 /r 之前的数据已经写入文件，退出循环，等待接收更多数据后再进入该循环
                                    if(endIndex == 0){   
                                        break;
                                    }

                                    // 否则将 endIndex 前的数据写入文件
                                    saveLen = endIndex;
                                }
                            }
                            // 如果没有退出表示当前仍是数据部分，将 saveLen 字节的数据存入文件，并将这些数据从 recvMsg 数据中删除
                            ofs.write(requestStatus[m_clientFd].recvMsg.c_str(), saveLen);
                            requestStatus[m_clientFd].recvMsg.erase(0, saveLen);
                        }
                        ofs.close();
                    }
                    // std::cout << "已退出文件接收函数" << std::endl;
                    // 如果文件已经处理完成，设置消息体为完成状态
                    if(requestStatus[m_clientFd].fileMsgStatus == FILE_COMPLATE){
                        // 设置响应消息的资源路径，在 HandleSend 中根据请求资源构建整个响应消息并发送
                        responseStatus[m_clientFd].bodyFileName = "/redirect"; 
                        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);   // 完成后重置可读事件和可写事件，用于发送重定向回复报文
                        requestStatus[m_clientFd].status = HADNLE_COMPLATE;
                        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的 POST 请求体处理完成，添加 Response 写事件，发送重定向报文刷新文件列表" << std::endl;
                        break;
                    }
                }else{    // POST 是其他类型的数据
                    // 其他 POST 类型的数据时，直接返回重定向报文，获取文件列表
                    responseStatus[m_clientFd].bodyFileName = "/redirect";
                    modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
                    requestStatus[m_clientFd].status = HADNLE_COMPLATE;
                    std::cout << outHead("error") << "客户端 " << m_clientFd << " 的 POST 请求中接收到不能处理的数据，添加 Response 写事件，返回重定向到文件列表的报文" << std::endl;
                    break;
                }
            }

        }

    }

    
    if(requestStatus[m_clientFd].status == HADNLE_COMPLATE){     // 如果请求处理完成，将该套接字对应的请求删除
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息处理成功" << std::endl;
        requestStatus.erase(m_clientFd);
    }else if(requestStatus[m_clientFd].status == HANDLE_ERROR){        
        // 请求处理错误，关闭该文件描述符，将该套接字对应的请求删除，从监听列表中删除该文件描述符
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息处理失败，关闭连接" << std::endl;
        // 先删除监听的文件描述符
        deleteWaitFd(m_epollFd, m_clientFd);
        // 再关闭文件描述符
        shutdown(m_clientFd, SHUT_RDWR);
        close(m_clientFd);
        requestStatus.erase(m_clientFd);
    }
    
}

// 处理向客户端发送数据
void HandleSend::process(){
    std::cout << outHead("info") << "开始处理客户端 " << m_clientFd << " 的一个 HandleSend 事件" << std::endl;
    // 如果该套接字没有需要处理的 Response 消息，直接退出
    if(responseStatus.find(m_clientFd) == responseStatus.end()){
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 没有要处理的响应消息" << std::endl;
        return;
    }

    // 根据 Response 对象的状态执行特定的处理

    // 如果处于初始状态，根据请求的文件构建不同类型的发送数据
    if(responseStatus[m_clientFd].status == HANDLE_INIT){
        // 首先分离操作方法和文件
        std::string opera, filename;
        if(responseStatus[m_clientFd].bodyFileName == "/"){
            // 如果是访问根目录，下面会直接返回文件列表
            opera = "/";
        }else{
            // 如果不是访问根目录，根据 / 对URL中的路径（如 /delete/filename）进行分隔，找到要执行的操作和操作的文件

            // 文件名的查找中间 / 的索引
            int i = 1;
            while(i < responseStatus[m_clientFd].bodyFileName.size() && responseStatus[m_clientFd].bodyFileName[i] != '/'){
                ++i;
            }
            // 检查是否包含操作和对应的文件名，如果不满足 操作+文件名 的格式，设置为重定向操作，将页面重定向到文件列表页面
            if(i < responseStatus[m_clientFd].bodyFileName.size() - 1){
                opera = responseStatus[m_clientFd].bodyFileName.substr(1, i - 1);
                filename = responseStatus[m_clientFd].bodyFileName.substr(i+1);
            }else{
                opera = "redirect";
            }

        }
        

        // 初始状态中，根据资操作确定所发送数据的内容
        if(opera == "/"){                   //如果是根目录，返回文件夹中的所有文件名字
            // 添加状态行
            responseStatus[m_clientFd].beforeBodyMsg = getStatusLine("HTTP/1.1", "200", "OK");

            // 先创建响应体对应的数据
            // 函数中先获取 /filedir 文件夹中的所有文件，然后根据 filelist.html 的页面结构，所有文件项加入页面，最终的HTML页面以字符串形式保存到 msgBody 中
            getFileListPage(responseStatus[m_clientFd].msgBody);
            // 记录页面的字节个数，即消息体长度
            responseStatus[m_clientFd].msgBodyLen = responseStatus[m_clientFd].msgBody.size();


            // 根据消息体的数据长度添加头部信息
            responseStatus[m_clientFd].beforeBodyMsg += getMessageHeader(std::to_string(responseStatus[m_clientFd].msgBodyLen), "html");
            // 加入空行
            responseStatus[m_clientFd].beforeBodyMsg += "\r\n";

            responseStatus[m_clientFd].beforeBodyMsgLen = responseStatus[m_clientFd].beforeBodyMsg.size();


            // 设置标识，转换到发送数据的状态
            responseStatus[m_clientFd].bodyType = HTML_TYPE;      // 设置消息体的类型
            responseStatus[m_clientFd].status = HANDLE_HEAD;      // 设置状态为等待发送消息头
            responseStatus[m_clientFd].curStatusHasSendLen = 0;   // 设置当前已发送的数据长度为0
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应消息用来返回文件列表页面，状态行和消息体已构建完成" << std::endl;

        }else if(opera == "download"){      // 下载文件
            // 构建下载文件的响应，向用户发送文件

            // 添加状态行
            responseStatus[m_clientFd].beforeBodyMsg = getStatusLine("HTTP/1.1", "200", "OK");

            // 添加URL解码逻辑（示例）
            std::string decodedFilename = urlDecode(filename);  // 新增：对文件名进行 URL 解码
            // responseStatus[m_clientFd].fileMsgFd = open(("filedir/" + filename).c_str(), O_RDONLY);
            // 使用解码后的文件名打开文件
            responseStatus[m_clientFd].fileMsgFd = open(("filedir/" + decodedFilename).c_str(), O_RDONLY);
            // 获取所传递文件的描述符
            if(responseStatus[m_clientFd].fileMsgFd == -1){                  // 文件打开失败时，退出当前函数（避免下面关闭文件造成错误），并重置写事件，在下次进入时回复重定向报文
                std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息要下载文件 " << filename << " ，但是文件打开失败，退出当前函数，重新进入用于返回重定向报文，重定向到文件列表" << std::endl;
                responseStatus[m_clientFd] = Response();                     // 重置 Response
                responseStatus[m_clientFd].bodyFileName = "/redirect";
                modifyWaitFd(m_epollFd, m_clientFd, true, true, true);       // 重置写事件
                return;
            }else{    // 文件打开成功时才构建响应体
                // 获取文件信息
                struct stat fileStat;
                fstat(responseStatus[m_clientFd].fileMsgFd, &fileStat);
                
                // 获取文件长度，作为消息体长度
                responseStatus[m_clientFd].msgBodyLen = fileStat.st_size;
                
                // 根据消息体构建消息首部
                responseStatus[m_clientFd].beforeBodyMsg += getMessageHeader(std::to_string(responseStatus[m_clientFd].msgBodyLen), "file", std::to_string(responseStatus[m_clientFd].msgBodyLen - 1));
                // 加入空行
                responseStatus[m_clientFd].beforeBodyMsg += "\r\n";
                responseStatus[m_clientFd].beforeBodyMsgLen = responseStatus[m_clientFd].beforeBodyMsg.size();
                
                // 设置标识，转换到发送数据的状态
                responseStatus[m_clientFd].bodyType = FILE_TYPE;      // 设置消息体的类型
                responseStatus[m_clientFd].status = HANDLE_HEAD;      // 设置状态为处理消息头
                responseStatus[m_clientFd].curStatusHasSendLen = 0;   // 设置当前已发送的数据长度为0

                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息要下载文件 " << filename << " ，文件打开成功，根据文件构建响应消息状态行和头部信息成功" << std::endl;
                
            }

        }else if(opera == "delete"){        // 删除文件
            // 在本地删除文件
            int ret = remove(("filedir/" + filename).c_str());
            if(ret != 0){
                std::cout << outHead("error") << "客户端 " << m_clientFd << " 的请求消息要删除文件 " << filename << " 但是文件删除失败" << std::endl;
            }else{
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息要删除文件 " << filename << " 且文件删除成功" << std::endl;
            }

            // 不管文件删除成功还是失败，都重定向到文件列表页面
            responseStatus[m_clientFd] = Response();                     // 重置 Response
            responseStatus[m_clientFd].bodyFileName = "/redirect";       // 设置为重定向报文

            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的请求消息处理完成，发送重定向报文" << std::endl;

            // 重置 EPOLLONESHOT，之后直接退出函数，使得可以再次进入函数重新构建文件列表的响应消息
            modifyWaitFd(m_epollFd, m_clientFd, true, true, true);
            return;
        }else{                              // 对于其他的请求，将页面全部重定向到文件列表页面
            // 添加状态行
            responseStatus[m_clientFd].beforeBodyMsg = getStatusLine("HTTP/1.1", "302", "Moved Temporarily");

            // 构建重定向的消息首部
            responseStatus[m_clientFd].beforeBodyMsg += getMessageHeader("0", "html", "/", "");

            // 加入空行
            responseStatus[m_clientFd].beforeBodyMsg += "\r\n";

            responseStatus[m_clientFd].beforeBodyMsgLen = responseStatus[m_clientFd].beforeBodyMsg.size();

            // 设置标识，转换到发送数据的状态
            responseStatus[m_clientFd].bodyType = EMPTY_TYPE;    // 设置消息体的类型
            responseStatus[m_clientFd].status = HANDLE_HEAD;     // 设置状态为处理消息头
            responseStatus[m_clientFd].curStatusHasSendLen = 0;   // 设置当前已发送的数据长度为0
            std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应报文是重定向报文，状态行和消息首部已构建完成" << std::endl;
        }
    }

    while(1){
        long long sentLen = 0;
        // 发送响应消息头
        if(responseStatus[m_clientFd].status == HANDLE_HEAD){
            // 开始发送消息体之前的所有数据
            sentLen = responseStatus[m_clientFd].curStatusHasSendLen;
            sentLen = send(m_clientFd, responseStatus[m_clientFd].beforeBodyMsg.c_str() + sentLen, responseStatus[m_clientFd].beforeBodyMsgLen - sentLen, 0);
            if(sentLen == -1) {
                if(errno != EAGAIN){
                    // 如果不是缓冲区满，设置发送失败状态，并退出循环
                    requestStatus[m_clientFd].status = HANDLE_ERROR;
                    std::cout << outHead("error") << "发送响应体和消息首部时返回 -1 (errno = " << errno << ")" << std::endl;
                    break;
                }
                // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                break;
            }
            responseStatus[m_clientFd].curStatusHasSendLen += sentLen;
            // 如果数据已经发送完成，将状态设置为发送消息体
            if(responseStatus[m_clientFd].curStatusHasSendLen >= responseStatus[m_clientFd].beforeBodyMsgLen){
                responseStatus[m_clientFd].status = HANDLE_BODY;     // 设置为正在处理消息体的状态
                responseStatus[m_clientFd].curStatusHasSendLen = 0;   // 设置已经发送的数据长度为 0
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 响应消息的状态行和消息首部发送完成，正在发送消息体..." << std::endl;
            }

            // 如果发送的是文件，输出提示信息
            if(responseStatus[m_clientFd].bodyType == FILE_TYPE){
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的是文件，开始发送文件 " << responseStatus[m_clientFd].bodyFileName << " ..." << std::endl;
            }
        }

        // 发送响应消息体
        if(responseStatus[m_clientFd].status == HANDLE_BODY){
            // 根据发送数据的类型执行特定的发送操作
            if(responseStatus[m_clientFd].bodyType == HTML_TYPE){
                // 消息体为 HTML 页面时的发送方法
                sentLen = responseStatus[m_clientFd].curStatusHasSendLen;
                sentLen = send(m_clientFd, responseStatus[m_clientFd].msgBody.c_str() + sentLen, responseStatus[m_clientFd].msgBodyLen - sentLen, 0);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        // 如果不是缓冲区满，设置发送失败状态，并退出循环
                        requestStatus[m_clientFd].status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送 HTML 消息体时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    
                    // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                    break;
                }
                responseStatus[m_clientFd].curStatusHasSendLen += sentLen;
                
                // 如果数据已经发送完成，将状态设置为发送消息体
                if(responseStatus[m_clientFd].curStatusHasSendLen >= responseStatus[m_clientFd].msgBodyLen){
                    responseStatus[m_clientFd].status = HADNLE_COMPLATE;     // 设置为正在处理消息体的状态
                    responseStatus[m_clientFd].curStatusHasSendLen = 0;   // 设置已经发送的数据长度为 0
                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的是 HTML 文件，文件发送成功" << std::endl;
                    break;
                }

            }else if(responseStatus[m_clientFd].bodyType == FILE_TYPE){
                // 消息体是文件时的发送方法
                
                // 获取已经发送的字节数，用来控制下面函数从哪个地方开始发送
                sentLen = responseStatus[m_clientFd].curStatusHasSendLen;
                
                // 使用 sendfile 函数，实现零拷贝的发送数据，提高效率
                sentLen = sendfile(m_clientFd, responseStatus[m_clientFd].fileMsgFd, (off_t *)&sentLen, responseStatus[m_clientFd].msgBodyLen - sentLen);
                if(sentLen == -1){
                    if(errno != EAGAIN){
                        // 如果不是缓冲区满，设置发送失败状态
                        requestStatus[m_clientFd].status = HANDLE_ERROR;
                        std::cout << outHead("error") << "发送文件时返回 -1 (errno = " << errno << ")" << std::endl;
                        break;
                    }
                    // 如果缓冲区已满，退出循环，下面会重置 EPOLLOUT 事件，等待下次进入函数继续发送
                    break;
                }
                
                // 累加已发送的数据长度
                responseStatus[m_clientFd].curStatusHasSendLen += sentLen;

                // 文件发送完成后，重置 Response 为访问根目录的响应，向客户端传递文件列表
                if(responseStatus[m_clientFd].curStatusHasSendLen >= responseStatus[m_clientFd].msgBodyLen){
                    responseStatus[m_clientFd].status = HADNLE_COMPLATE;     // 设置为事件处理完成
                    responseStatus[m_clientFd].curStatusHasSendLen = 0;       // 设置已经发送的数据长度为 0

                    std::cout << outHead("info") << "客户端 " << m_clientFd << " 请求的文件发送完成" << std::endl;
                    break;
                }

            }else if(responseStatus[m_clientFd].bodyType == EMPTY_TYPE){
                // 消息体为空时直接进入下个状态，目前用于重定向报文的消息体发送
                responseStatus[m_clientFd].status = HADNLE_COMPLATE;       // 设置为事件处理完成
                responseStatus[m_clientFd].curStatusHasSendLen = 0;         // 设置已经发送的数据长度为 0
                std::cout << outHead("info") << "客户端 " << m_clientFd << " 的重定向报文发送成功" << std::endl;
                break;
            }
        }

        if(responseStatus[m_clientFd].status == HANDLE_ERROR){    // 如果是出错状态，退出 while 处理
            break;
        }
    }
    

    // 判断发送最终状态执行特定的操作
    if(responseStatus[m_clientFd].status == HADNLE_COMPLATE){
        // 完成发送数据后删除该响应
        responseStatus.erase(m_clientFd);
        modifyWaitFd(m_epollFd, m_clientFd, true, true, false);                            // 不再监听写事件
        std::cout << outHead("info") << "客户端 " << m_clientFd << " 的响应报文发送成功" << std::endl;
    }else if(responseStatus[m_clientFd].status == HANDLE_ERROR){
        // 如果发送失败，删除该响应，删除监听该文件描述符，关闭连接
        responseStatus.erase(m_clientFd);
        // 不再监听写事件
        modifyWaitFd(m_epollFd, m_clientFd, true, false, false);
        // 关闭文件描述符
        shutdown(m_clientFd, SHUT_WR);
        close(m_clientFd);
        std::cout << outHead("error") << "客户端 " << m_clientFd << " 的响应报文发送失败，关闭相关的文件描述符" << std::endl;
    }else{                      // 如果不是完成了数据传输或出错，应该重置 EPOLLSHOT 事件，保证写事件可以继续产生，继续传输数据
        modifyWaitFd(m_epollFd, m_clientFd, true, true, true);

        // 退出函数，当执行失败时或数据传输完成时才需要关闭文件
        return;
    }

    // 处理成功或非文件打开失败时需要关闭文件
    if(responseStatus[m_clientFd].bodyType == FILE_TYPE){
        close(responseStatus[m_clientFd].fileMsgFd);
    }

}

// 用于构建状态行，参数分别表示状态行的三个部分
std::string HandleSend::getStatusLine(const std::string &httpVersion, const std::string &statusCode, const std::string &statusDes){
    std::string statusLine;
    // 记录状态行相关的参数
    responseStatus[m_clientFd].responseHttpVersion = httpVersion;
    responseStatus[m_clientFd].responseStatusCode = statusCode;
    responseStatus[m_clientFd].responseStatusDes = statusDes;
    // 构建状态行
    statusLine = httpVersion + " ";
    statusLine += statusCode + " ";
    statusLine += statusDes + "\r\n";

    return statusLine;
}

// 以下两个函数用来构建文件列表的页面，最终结果保存到 fileListHtml 中
void HandleSend::getFileListPage(std::string &fileListHtml){
    // 结果保存到 fileListHtml

    // 将指定目录内的所有文件保存到 fileVec 中
    std::vector<std::string> fileVec;
    getFileVec("filedir", fileVec);
    
    // 构建页面
    std::ifstream fileListStream("html/filelist.html", std::ios::in);
    std::string tempLine;
    // 首先读取文件列表的 <!--filelist_label--> 注释前的语句
    while(1){
        getline(fileListStream, tempLine);
        if(tempLine == "<!--filelist_label-->"){
            break;
        }
        fileListHtml += tempLine + "\n";
    }

    // 根据如下标签，将将文件夹中的所有文件项添加到返回页面中
    //             <tr><td class="col1">filenamename</td> <td class="col2"><a href="file/filename">下载</a></td> <td class="col3"><a href="delete/filename">删除</a></td></tr>
    for(const auto &filename : fileVec){
        fileListHtml += "            <tr><td class=\"col1\">" + filename +
                    "</td> <td class=\"col2\"><a href=\"download/" + filename +
                    "\">下载</a></td> <td class=\"col3\"><a href=\"delete/" + filename +
                    "\" onclick=\"return confirmDelete();\">删除</a></td></tr>" + "\n";
    }

    // 将文件列表注释后的语句加入后面
    while(getline(fileListStream, tempLine)){
        fileListHtml += tempLine + "\n";
    }
    
}
/**
 * @brief 获取指定目录下的所有文件名并存储在结果向量中
 *
 * 使用 dirent 库获取指定目录下的所有文件名，并将其存储在传入的结果向量中。
 *
 * @param dirName 指定目录的路径
 * @param resVec 用于存储获取到的文件名的结果向量
 */
void HandleSend::getFileVec(const std::string dirName, std::vector<std::string> &resVec){
    // 使用 dirent 获取文件目录下的所有文件
    DIR *dir;   // 目录指针
    dir = opendir(dirName.c_str());
    struct dirent *stdinfo;
    while (1)
    {
        // 获取文件夹中的一个文件
        stdinfo = readdir(dir);
        if (stdinfo == nullptr){
            break;
        }
        resVec.push_back(stdinfo->d_name);
        if(resVec.back() == "." || resVec.back() == ".."){
            resVec.pop_back();
        }
    }
}

// 构建头部字段：
// contentLength        : 指定消息体的长度
// contentType          : 指定消息体的类型
// redirectLoction = "" : 如果是重定向报文，可以指定重定向的地址。空字符串表示不添加该首部。
// contentRange = ""    : 如果是下载文件的响应报文，指定当前发送的文件范围。空字符串表示不添加该首部。
/**
 * @brief 生成HTTP响应头字符串
 *
 * 根据输入的内容长度、内容类型、重定向位置和内容范围生成HTTP响应头字符串。
 *
 * @param contentLength 内容长度字符串
 * @param contentType 内容类型字符串
 * @param redirectLoction 重定向位置字符串
 * @param contentRange 内容范围字符串
 *
 * @return 返回生成的HTTP响应头字符串
 */
std::string HandleSend::getMessageHeader(const std::string contentLength, const std::string contentType, const std::string redirectLoction, const std::string contentRange){
    std::string headerOpt;

    // 添加消息体长度字段
    if(contentLength != ""){
        headerOpt += "Content-Length: " + contentLength + "\r\n";
    }

    // 添加消息体类型字段
    if(contentType != ""){
        if(contentType == "html"){
            headerOpt += "Content-Type: text/html;charset=UTF-8\r\n";     // 发送网页时指定的类型
        }else if(contentType == "file"){
            headerOpt += "Content-Type: application/octet-stream\r\n";    // 发送文件时指定的类型
        }
    }

    // 添加重定向位置字段
    if(redirectLoction != ""){
        headerOpt += "Location: " + redirectLoction + "\r\n";
    }

    // 添加文件范围的字段
    if(contentRange != ""){
        headerOpt += "Content-Range: 0-" + contentRange + "\r\n";
    }

    headerOpt += "Connection: keep-alive\r\n";

    return headerOpt;
}
