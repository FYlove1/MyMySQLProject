// ClientConnectionManager.h
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

class ClientConnectionManager {
public:
    ClientConnectionManager();
    ~ClientConnectionManager();

    void startServer(int port);
    void stopServer();
    void acceptConnections();
private:
    int listenSocket;
    std::vector<int> clientSockets;
    // 定义客户端会话结构体
    struct ClientSession {
        int sockfd;          // 套接字描述符
        std::string account; // 用户账号
        std::string ip;      // 客户端IP
        time_t loginTime;    // 登录时间戳
    };

    // 使用线程安全容器存储
    std::unordered_map<std::string, ClientSession> accountToSession;  // Key=Account
    std::unordered_map<int, std::string> socketToAccount;             // Key=Socket
    std::mutex sessionMutex; // 互斥锁保证线程安全


    std::mutex clientMutex;
    bool running;
    //获取当前时间戳
    std::string getCurrentTimestamp();


    void handleClient(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);


    //解析用户的发送，并且进行回复
    nlohmann::json ResponseJsonToClient(const nlohmann::json &ReceviedJson,int clientsocket);
    bool HandleLoginEvent(const nlohmann::json &ReceviedJson,int clientsocket);
};

#endif // CHATSERVER_CLIENTCONNECTIONMANAGER_H
