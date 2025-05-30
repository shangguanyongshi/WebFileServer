# http服务器发送请求和接收请求，有公共的地方吗
是的，**HTTP 服务器发送请求（客户端行为）和接收请求（服务端行为）确实有一些“公共的地方”**，尤其在以下几个方面：

---

## ✅ 1. **都遵循 HTTP 协议格式**

无论是发送还是接收请求，都遵循统一的 [HTTP 报文格式](https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Messages)，这意味着：

* 都有 **请求行 / 状态行**
* 都有 **首部字段（Header）**
* 都可以包含 **消息体（Body）**

### 示例：HTTP 请求报文（客户端发给服务端）

```
GET /index.html HTTP/1.1
Host: example.com
User-Agent: curl/7.68.0
Accept: */*
```

### 示例：HTTP 响应报文（服务端发给客户端）

```
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 1234

<html>...</html>
```

---

## ✅ 2. **都需要解析或构造 HTTP 报文**

* 服务端 **接收请求** 时，需要**解析**请求报文（解析请求行、头部、body）
* 客户端 **发送请求** 时，需要**构造**请求报文（拼接请求行、头部、body）
* 服务端 **发送响应** 时，需要**构造**响应报文
* 客户端 **接收响应** 时，需要**解析**响应报文

👉 所以无论是“发送”还是“接收”，都用到了类似的报文处理逻辑（构造、拆解字符串，识别 header 等）

---

## ✅ 3. **都可能使用 socket 发送和接收数据**

无论是 HTTP 客户端还是服务端，底层通常使用 socket：

* `send()`：发送 HTTP 请求或响应
* `recv()`：接收 HTTP 请求或响应

在这一点上，**底层传输逻辑是对称的**，只是角色不同。

---

## ✅ 4. **都处理超时、关闭连接等状态**

无论是发送请求还是接收请求，都要：

* 设置 socket 超时
* 判断连接是否关闭
* 支持 `keep-alive` 或短连接

---

## 🧠 总结：可复用的“公共逻辑”有哪些？

| 模块          | 说明                                              |
| ----------- | ----------------------------------------------- |
| HTTP 报文解析模块 | 统一处理请求报文或响应报文的解析                                |
| 报文构造模块      | 构造请求或响应字符串                                      |
| 编码/解码模块     | URL 编码、Header 编码、JSON 编解码等                      |
| socket 收发封装 | 统一的 `send_http_message()`、`recv_http_message()` |
| 错误码处理模块     | 错误码统一处理，如 `404`, `500`, `400`                   |

---

如果你自己在写一个 HTTP 服务器和客户端，可以将“公共部分”抽象成通用模块，比如：

```cpp
// 通用的 HTTP 报文结构体
struct HttpMessage {
    std::string method_or_status;  // GET / 200 OK 等
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};
```

然后写：

* `parse_http_message()`：请求或响应都能解析
* `build_http_message()`：请求或响应都能生成

---
