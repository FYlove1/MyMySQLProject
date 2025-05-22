// ClientConnectionManager.h
#ifndef CHATSERVER_CLIENTCONNECTIONMANAGER_H
#define CHATSERVER_CLIENTCONNECTIONMANAGER_H

#include <vector>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
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


    void handleClient(int clientSocket);
    void processMessage(int clientSocket, const std::string& message);
    nlohmann::json ResponseJsonToClient(const nlohmann::json &ReceviedJson);
};

#endif // CHATSERVER_CLIENTCONNECTIONMANAGER_H
