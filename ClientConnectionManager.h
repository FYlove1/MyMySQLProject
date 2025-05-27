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
    std::mutex clientMutex;
    bool running;
    //获取当前时间戳
    std::string getCurrentTimestamp();


    void handleClient(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);


    //解析用户的发送，并且进行回复
    nlohmann::json ResponseJsonToClient(const nlohmann::json &ReceviedJson);
    bool HandleLoginEvent(const nlohmann::json &ReceviedJson);
};

#endif // CHATSERVER_CLIENTCONNECTIONMANAGER_H
