#ifndef CHATSERVER_CLIENTCONNECTIONMANAGER_H
#define CHATSERVER_CLIENTCONNECTIONMANAGER_H

#include <vector>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <sys/epoll.h>
#include <memory>
#include "ThreadPool.h"

class ClientConnectionManager {
public:
    ClientConnectionManager();
    ~ClientConnectionManager();

    bool startServer(int port);
    void stopServer();

    // 主要的事件处理函数，用于在主线程中调用
    int processEvents(int timeout_ms = 100);  // 返回处理的事件数量，-1表示错误
    bool isRunning() const { return running; }

private:
    // 定义客户端会话结构体
    struct ClientSession {
        int sockfd;          // 套接字描述符
        std::string account; // 用户账号
        std::string ip;      // 客户端IP
        time_t loginTime;    // 登录时间戳
    };

    // 客户端连接状态
    struct ClientConnection {
        int sockfd;
        std::vector<char> readBuffer;    // 读缓冲区
        std::vector<char> writeBuffer;   // 写缓冲区
        uint32_t expectedSize;           // 期望读取的消息大小
        bool readingHeader;              // 是否正在读取消息头
        size_t bytesRead;                // 已读取字节数
        size_t bytesToWrite;             // 待写入字节数
        size_t bytesWritten;             // 已写入字节数
        std::mutex writeMutex;           // 保护写操作
    };

    int listenSocket;
    int epollFd;
    bool running;

    // 线程池
    //std::unique_ptr<ThreadPool> threadPool;

    // 客户端连接管理
    std::unordered_map<int, std::unique_ptr<ClientConnection>> connections; //key=socketID
    std::unordered_map<std::string, ClientSession> accountToSession;  // Key=Account
    std::unordered_map<int, std::string> socketToAccount;             // Key=Socket
    std::mutex connectionMutex;
    std::mutex sessionMutex;
    std::mutex SqlMutex;

    // 核心方法
    void handleNewConnection();          // 处理新连接
    void handleClientRead(int clientFd); // 处理客户端读事件
    void handleClientWrite(int clientFd);// 处理客户端写事件
    void closeConnection(int clientFd);  // 关闭连接

    // 异步消息处理方法
    void processCompleteMessageAsync(int clientFd, const std::string& message);
    void sendResponseAsync(int clientFd, const std::string& response);

    // 同步消息处理方法（在线程池中执行）
    void processCompleteMessage(int clientFd, const std::string& message);
    void sendResponse(int clientFd, const std::string& response);

    // 辅助方法
    std::string getCurrentTimestamp();
    void setNonBlocking(int fd);
    bool addToEpoll(int fd, uint32_t events);
    bool modifyEpoll(int fd, uint32_t events);
    bool removeFromEpoll(int fd);

    //健康检查
    void checkConnectionHealth(int clientFd);
    //业务部分??  上面是 整个服务器的基本组成,是基础,下面是 相关的业务处理
    // JSON处理方法
    nlohmann::json ResponseJsonToClient(const nlohmann::json &ReceivedJson, int clientsocket);
    //登录
    bool HandleLoginEvent(const nlohmann::json &ReceivedJson, int clientsocket);
    //添加好友
    bool HandleAddFriendEvent(const nlohmann::json &ReceivedJson, int clientsocket);

    int HandleSendtoFriendEvent(const nlohmann::json &ReceivedJson, int clientsocket);

};

#endif // CHATSERVER_CLIENTCONNECTIONMANAGER_H