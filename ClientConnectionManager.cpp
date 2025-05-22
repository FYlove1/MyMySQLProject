//
// Created by fylove on 25-5-20.
//

#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>
#include "ClientConnectionManager.h"
#include <arpa/inet.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
ClientConnectionManager::ClientConnectionManager() : listenSocket(-1), running(false) {}

ClientConnectionManager::~ClientConnectionManager() {
    stopServer();
}

void ClientConnectionManager::startServer(int port) {
    if (running) {
        std::cerr << "Server is already running." << std::endl;
        return;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Failed to bind socket." << std::endl;
        close(listenSocket);
        return;
    }

    if (listen(listenSocket, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on socket." << std::endl;
        close(listenSocket);
        return;
    }

    running = true;
    std::thread(&ClientConnectionManager::acceptConnections, this).detach();
    std::cout << "Server started on port " << port << "." << std::endl;
}

void ClientConnectionManager::stopServer() {
    if (!running) {
        return;
    }

    running = false;
    close(listenSocket);

    std::lock_guard<std::mutex> lock(clientMutex);
    for (int clientSocket : clientSockets) {
        close(clientSocket);
    }
    clientSockets.clear();
    std::cout << "Server stopped." << std::endl;
}

void ClientConnectionManager::acceptConnections() {
    while (running) {
        sockaddr_in clientAddr{};
        socklen_t clientAddrSize = sizeof(clientAddr);
        int clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);

        if (clientSocket != -1) {
            std::lock_guard<std::mutex> lock(clientMutex);
            clientSockets.push_back(clientSocket);
            std::cout << "New connection from " << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << std::endl;

            std::thread(&ClientConnectionManager::handleClient, this, clientSocket).detach();
        }
    }
}

void ClientConnectionManager::handleClient(int clientSocket) {
    // 设置 recv 超时时间为 5 秒
    struct timeval timeout;
    timeout.tv_sec = 5;  // 5秒接收超时
    timeout.tv_usec = 0;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    while (running) {
        // 步骤一：接收 4 字节的长度
        uint32_t networkSize;
        ssize_t totalReceived = 0;
        char* sizeBuffer = reinterpret_cast<char*>(&networkSize);
        while (totalReceived < sizeof(uint32_t)) {
            ssize_t bytesReceived = recv(clientSocket, sizeBuffer + totalReceived,
                                         sizeof(uint32_t) - totalReceived, 0);
            if (bytesReceived <= 0) {
                std::cerr << "Failed to receive message length." << std::endl;
                goto cleanup;
            }
            totalReceived += bytesReceived;
        }
        uint32_t messageSize = ntohl(networkSize);  // 转换为主机字节序
        // 步骤二：接收指定长度的消息内容
        std::vector<char> messageBuffer(messageSize);
        totalReceived = 0;

        while (totalReceived < static_cast<ssize_t>(messageSize)) {
            ssize_t bytesReceived = recv(clientSocket, messageBuffer.data() + totalReceived,
                                         messageSize - totalReceived, 0);
            if (bytesReceived <= 0) {
                std::cerr << "Failed to receive full message data." << std::endl;
                goto cleanup;
            }
            totalReceived += bytesReceived;
        }

        // 步骤三：解析 JSON 并构造响应
        try {
            std::string message(messageBuffer.data(), messageSize);
            std::cout << "Received: " << message << std::endl;

            json j_ReceviedJson = json::parse(message);
            std::string userMessage = j_ReceviedJson.value("message", "");




            if (!userMessage.empty()) {
                // 构造响应 JSON
                json response;
                response["response"] = "Echo: " + userMessage;
                std::string responseStr = response.dump();

                // 发送响应（先发长度，再发内容）
                uint32_t responseSize = htonl(static_cast<uint32_t>(responseStr.size()));
                send(clientSocket, &responseSize, sizeof(responseSize), 0);
                send(clientSocket, responseStr.c_str(), responseStr.size(), 0);
            } else {
                const char* error = "Invalid message format";
                uint32_t errorSize = htonl(static_cast<uint32_t>(strlen(error)));
                send(clientSocket, &errorSize, sizeof(errorSize), 0);
                send(clientSocket, error, strlen(error), 0);
            }
        } catch (const json::exception& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
            const char* error = "Invalid JSON";
            uint32_t errorSize = htonl(static_cast<uint32_t>(strlen(error)));
            send(clientSocket, &errorSize, sizeof(errorSize), 0);
            send(clientSocket, error, strlen(error), 0);
        }
    }

cleanup:
    // 清理客户端连接
    std::lock_guard<std::mutex> lock(clientMutex);
    auto it = std::find(clientSockets.begin(), clientSockets.end(), clientSocket);
    if (it != clientSockets.end()) {
        clientSockets.erase(it);
        close(clientSocket);
        std::cout << "Connection closed by client." << std::endl;
    }
}


// 处理客户端的json  数据 ，并且书写返回给客户端的json
nlohmann::json ClientConnectionManager::ResponseJsonToClient(const nlohmann::json &ReceviedJson) {

}

