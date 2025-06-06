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
#include "Sql_Manager.h"
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

std::string ClientConnectionManager::getCurrentTimestamp() {
    // 获取当前时间点
    auto now = std::chrono::system_clock::now();
    // 转换为时间戳（秒）
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    // 转换为本地时间结构体
    std::tm local_tm = *std::localtime(&now_time_t);

    // 使用 stringstream 构造格式化时间字符串
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void ClientConnectionManager::handleClient(int clientSocket) {
    // 设置 recv 超时时间为 5 秒
    struct timeval timeout;
    timeout.tv_sec = 20;  // 20秒接收超时
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

            json client_send_json = json::parse(message);
            std::string request_type  = client_send_json.value("Type", "0");
            if (!request_type.empty()) {
                // 构造响应 JSON
                json response_client_Json = ResponseJsonToClient(client_send_json,clientSocket);
                std::string responseStr = response_client_Json.dump();


                // 发送长度
                uint32_t responseSize = htonl(static_cast<uint32_t>(responseStr.size()));
                int bytesSent = 0;
                while (bytesSent < sizeof(responseSize)) {
                    int sent = send(clientSocket, reinterpret_cast<const char*>(&responseSize) + bytesSent,
                                    sizeof(responseSize) - bytesSent, 0);
                    if (sent <= 0) {
                        // 处理错误
                        break;
                    }
                    bytesSent += sent;
                }

                // 发送前验证 JSON 字符串是否合法
                try {
                    json::parse(responseStr);  // 验证 JSON 是否合法
                } catch (const json::exception& e) {
                    std::cerr << "JSON 构造失败: " << e.what() << std::endl;
                    return;
                }
                // 发送JSON数据
                sleep(1);
                bytesSent = 0;
                while (bytesSent < responseStr.size()) {
                    int sent = send(clientSocket, responseStr.c_str() + bytesSent,
                                    responseStr.size() - bytesSent, 0);
                    if (sent <= 0) {
                        // 处理错误
                        break;
                    }
                    bytesSent += sent;
                }
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
    std::lock_guard<std::mutex> lock2(sessionMutex);

    auto it = std::find(clientSockets.begin(), clientSockets.end(), clientSocket);
    if (it != clientSockets.end()) {
        // 从 socketToAccount 获取账户信息
        auto accountIter = socketToAccount.find(clientSocket);
        if (accountIter != socketToAccount.end()) {
            // 从 accountToSession 删除记录
            accountToSession.erase(accountIter->second);
            // 从 socketToAccount 删除记录
            socketToAccount.erase(accountIter);
        }

        clientSockets.erase(it);
        close(clientSocket);
        std::cout << "Connection closed by client (Socket: " << clientSocket
                << ")." << std::endl;
    }
}



// 处理客户端的json  数据 ，并且书写返回给客户端的json
nlohmann::json ClientConnectionManager::ResponseJsonToClient(const nlohmann::json &ReceivedJson,int clientsocket) {
    json response_to_client;
    // 获取请求类型
    std::string requestType = ReceivedJson.value("Type", "0");
    if (requestType == "1") {
        // 登录请求
        bool loginSuccess = HandleLoginEvent(ReceivedJson,clientsocket);

        if (loginSuccess) {
            std::lock_guard<std::mutex> lock(sessionMutex);

            // 获取客户端地址信息
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            if (getpeername(clientsocket, (struct sockaddr*)&clientAddr, &clientAddrLen) == -1) {
                std::cerr << "获取客户端地址失败: " << strerror(errno) << std::endl;
                return false;
            }

            // 转换IP和端口
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
            uint16_t port = ntohs(clientAddr.sin_port);

            // 构造完整客户端信息
            ClientSession session{
                clientsocket,
                ReceivedJson["Account"],
                std::string(ipStr) + ":" + std::to_string(port), // 组合IP:Port
                time(nullptr)
            };

            // 更新映射关系
            accountToSession[ReceivedJson["Account"]] = session;
            socketToAccount[clientsocket] = ReceivedJson["Account"];
        }

        response_to_client = {
            {"ResponseType", "OperationResult"},
            {"OperationType", 1},
            {"Success", loginSuccess ? "1" : "0"},
            {"Message", loginSuccess ? "操作成功" : "登录失败"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }
    else {
        // 其他未知请求类型
        response_to_client = {
            {"ResponseType", "UnknownRequest"},
            {"Success", 0},
            {"Message", "未知请求类型"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }
    std::cout<<"Return Json"<<response_to_client.dump()<<std::endl;
    return response_to_client;
}


bool ClientConnectionManager::HandleLoginEvent(const nlohmann::json &ReceivedJson,int clientsocket) {



    Sql_Manager  &Sql_Manager = Sql_Manager::getInstance();
    return Sql_Manager.GoOnline(ReceivedJson["Account"],  ReceivedJson["Content"]);
}




