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
#include <fcntl.h>
#include <cerrno>
#include <sys/epoll.h>

using json = nlohmann::json;

ClientConnectionManager::ClientConnectionManager() : listenSocket(-1), epollFd(-1), running(false) {

    // 初始化线程池最大队列大小
    ThreadPool::getInstance().setMaxQueueSize(1000);
    std::cout << "ClientConnectionManager: 使用线程池单例，最大队列大小为1000" << std::endl;
}

ClientConnectionManager::~ClientConnectionManager() {
    stopServer();
}

bool ClientConnectionManager::startServer(int port) {
    if (running) {
        std::cerr << "Server is already running." << std::endl;
        return false;
    }

    // 创建监听socket
    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1) {
        std::cerr << "Failed to create socket." << std::endl;
        return false;
    }

    // 设置 SO_REUSEADDR
    int opt = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR." << std::endl;
        close(listenSocket);
        return false;
    }

    // 设置非阻塞
    setNonBlocking(listenSocket);

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        std::cerr << "Failed to bind socket." << std::endl;
        close(listenSocket);
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == -1) {
        std::cerr << "Failed to listen on socket." << std::endl;
        close(listenSocket);
        return false;
    }

    // 创建 epoll 实例
    epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd == -1) {
        std::cerr << "Failed to create epoll instance." << std::endl;
        close(listenSocket);
        return false;
    }

    // 将监听socket添加到epoll
    if (!addToEpoll(listenSocket, EPOLLIN)) {
        std::cerr << "Failed to add listen socket to epoll." << std::endl;
        close(epollFd);
        close(listenSocket);
        return false;
    }

    running = true;
    std::cout << "Server started on port " << port << " with epoll and thread pool." << std::endl;
    return true;
}

void ClientConnectionManager::stopServer() {
    if (!running) {
        return;
    }

    running = false;

    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        for (auto& pair : connections) {
            close(pair.first);
        }
        connections.clear();
    }

    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        accountToSession.clear();
        socketToAccount.clear();
    }

    if (epollFd != -1) {
        close(epollFd);
        epollFd = -1;
    }

    if (listenSocket != -1) {
        close(listenSocket);
        listenSocket = -1;
    }

    std::cout << "Server stopped." << std::endl;
}
//处理事件循环:
int ClientConnectionManager::processEvents(int timeout_ms) {
    if (!running || epollFd == -1) {
        return -1;
    }

    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, timeout_ms);

    if (nfds == -1) {
        if (errno == EINTR) {
            return 0; // 被信号中断，不是错误
        }
        std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
        return -1;
    }

    // 处理所有就绪的事件
    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        uint32_t eventMask = events[i].events;

        if (fd == listenSocket) {
            // 新连接 - 保持在主线程中处理，因为accept操作较轻量
            if (eventMask & EPOLLIN) {
                handleNewConnection();
            }
        } else {
            // 客户端事件 - 转移到线程池处理
            if (eventMask & (EPOLLERR | EPOLLHUP)) {
                std::cout << "Client " << fd << " error or hangup" << std::endl;

                // 将关闭连接操作提交到线程池
                auto [submitted, future] = ThreadPool::getInstance().enqueue(
                    [this, fd]() { this->closeConnection(fd); }
                );

                if (!submitted) {
                    // 如果线程池队列已满，在主线程中处理
                    closeConnection(fd);
                }
            } else {
                if (eventMask & EPOLLIN) {
                    // 读事件转移到线程池
                    auto [submitted, future] = ThreadPool::getInstance().enqueue(
                        [this, fd]() { this->handleClientRead(fd); }
                    );

                    if (!submitted) {
                        // 线程池队列满，在主线程中处理
                        handleClientRead(fd);
                    }
                }

                if (eventMask & EPOLLOUT) {
                    // 写事件转移到线程池
                    auto [submitted, future] = ThreadPool::getInstance().enqueue(
                        [this, fd]() { this->handleClientWrite(fd); }
                    );

                    if (!submitted) {
                        // 线程池队列满，在主线程中处理
                        handleClientWrite(fd);
                    }
                }
            }
        }
    }

    return nfds; // 返回处理的事件数量
}

void ClientConnectionManager::handleNewConnection() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t clientAddrSize = sizeof(clientAddr);
        int clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);

        if (clientSocket == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 没有更多连接
            } else {
                std::cerr << "Accept failed: " << strerror(errno) << std::endl;
                break;
            }
        }

        // 设置客户端socket为非阻塞
        setNonBlocking(clientSocket);

        // 添加到epoll
        if (!addToEpoll(clientSocket, EPOLLIN | EPOLLET)) {
            std::cerr << "Failed to add client socket to epoll." << std::endl;
            close(clientSocket);
            continue;
        }

        // 创建连接对象
        auto connection = std::make_unique<ClientConnection>();
        connection->sockfd = clientSocket;
        connection->readingHeader = true;
        connection->expectedSize = 4; // 先读取4字节消息长度
        connection->bytesRead = 0;
        connection->bytesToWrite = 0;
        connection->bytesWritten = 0;
        connection->readBuffer.resize(4);

        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            connections[clientSocket] = std::move(connection);
        }

        std::cout << "New connection from " << inet_ntoa(clientAddr.sin_addr)
                  << ":" << ntohs(clientAddr.sin_port) << " (fd: " << clientSocket << ")" << std::endl;
    }
}

void ClientConnectionManager::handleClientRead(int clientFd) {
    std::unique_lock<std::mutex> lock(connectionMutex);
    auto it = connections.find(clientFd);
    if (it == connections.end()) {
        std::cout << "Connection " << clientFd << " not found" << std::endl;
        return;
    }

    auto& conn = it->second;
    lock.unlock(); // 早期释放锁

    while (true) {
        size_t remainingBytes = conn->expectedSize - conn->bytesRead;
        if (remainingBytes == 0) {
            if (conn->readingHeader) {
                // 解析消息长度
                uint32_t networkSize;
                memcpy(&networkSize, conn->readBuffer.data(), 4);
                uint32_t messageSize = ntohl(networkSize);

                std::cout << "[FD:" << clientFd << "] Received message size: " << messageSize << " bytes" << std::endl;

                if (messageSize > 1024 * 1024 || messageSize == 0) {
                    std::cerr << "[FD:" << clientFd << "] Invalid message size: " << messageSize << std::endl;
                    closeConnection(clientFd);
                    return;
                }

                // 准备读取消息体
                conn->readingHeader = false;
                conn->expectedSize = messageSize;
                conn->bytesRead = 0;
                conn->readBuffer.resize(messageSize);
                continue;
            } else {
                // 完成消息读取，提交到线程池处理
                std::string message(conn->readBuffer.begin(), conn->readBuffer.end());

                std::cout << "[FD:" << clientFd << "] Message received, length: " << message.length() << std::endl;

                // 处理消息
                processCompleteMessage(clientFd, message);

                // 重置状态
                conn->readingHeader = true;
                conn->expectedSize = 4;
                conn->bytesRead = 0;
                conn->readBuffer.resize(4);
                continue;
            }
        }

        // 继续读取数据
        ssize_t bytesRead = recv(clientFd, conn->readBuffer.data() + conn->bytesRead,
                                remainingBytes, 0);

        if (bytesRead > 0) {
            conn->bytesRead += bytesRead;
            std::cout << "[FD:" << clientFd << "] Read " << bytesRead << " bytes, total: "
                      << conn->bytesRead << "/" << conn->expectedSize << std::endl;
        } else if (bytesRead == 0) {
            std::cout << "[FD:" << clientFd << "] Client disconnected gracefully" << std::endl;
            closeConnection(clientFd);
            return;
        } else {
            int error = errno;
            if (error == EAGAIN || error == EWOULDBLOCK) {
                break; // 没有更多数据
            } else if (error == ECONNRESET || error == EPIPE) {
                std::cout << "[FD:" << clientFd << "] Connection reset by peer" << std::endl;
                closeConnection(clientFd);
                return;
            } else {
                std::cerr << "[FD:" << clientFd << "] Read error: " << strerror(error) << std::endl;
                closeConnection(clientFd);
                return;
            }
        }
    }
}

void ClientConnectionManager::handleClientWrite(int clientFd) {

    std::unique_lock<std::mutex> lock(connectionMutex);

    auto it = connections.find(clientFd);
    if (it == connections.end()) {
        std::cout << "[FD:" << clientFd << "] Connection not found in write handler" << std::endl;
        return;
    }

    auto& conn = it->second;
    lock.unlock(); // 提前释放锁，避免长时间持有

    if (conn->bytesToWrite == 0) {
        std::cout << "[FD:" << clientFd << "] No data to write" << std::endl;
        return;
    }

    std::cout << "[FD:" << clientFd << "] Attempting to write "
              << (conn->bytesToWrite - conn->bytesWritten) << " bytes" << std::endl;

    while (conn->bytesWritten < conn->bytesToWrite) {
        size_t remainingBytes = conn->bytesToWrite - conn->bytesWritten;
        ssize_t bytesSent = send(clientFd, conn->writeBuffer.data() + conn->bytesWritten,
                                remainingBytes, MSG_NOSIGNAL);

        if (bytesSent > 0) {
            conn->bytesWritten += bytesSent;
            std::cout << "[FD:" << clientFd << "] Sent " << bytesSent << " bytes, total: "
                      << conn->bytesWritten << "/" << conn->bytesToWrite << std::endl;
        } else if (bytesSent == 0) {
            std::cout << "[FD:" << clientFd << "] Send returned 0, connection may be closed" << std::endl;
            closeConnection(clientFd);
            return;
        } else {
            int error = errno;
            if (error == EAGAIN || error == EWOULDBLOCK) {
                std::cout << "[FD:" << clientFd << "] Send would block, waiting for next opportunity" << std::endl;
                break;
            } else {
                std::cerr << "[FD:" << clientFd << "] Send error: " << strerror(error) << std::endl;
                closeConnection(clientFd);
                return;
            }
        }
    }

    // 发送完成，移除EPOLLOUT事件
    if (conn->bytesWritten >= conn->bytesToWrite) {
        conn->writeBuffer.clear();
        conn->bytesToWrite = 0;
        conn->bytesWritten = 0;

        if (!modifyEpoll(clientFd, EPOLLIN | EPOLLET)) {
            std::cerr << "[FD:" << clientFd << "] Failed to modify epoll after write completion" << std::endl;
        }

        std::cout << "[FD:" << clientFd << "] All data sent successfully" << std::endl;
    }
}

void ClientConnectionManager::closeConnection(int clientFd) {
    std::cout << "[FD:" << clientFd << "] Closing connection" << std::endl;

    removeFromEpoll(clientFd);
    close(clientFd);

    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        connections.erase(clientFd);
    }

    {
        std::lock_guard<std::mutex> lock(sessionMutex);
        auto accountIter = socketToAccount.find(clientFd);

        if (accountIter != socketToAccount.end()) {
            std::string account = accountIter->second;
            Sql_Manager::getInstance().GoOffline(account);
            accountToSession.erase(accountIter->second);
            socketToAccount.erase(accountIter);
        }
    }
}

// 处理消息的方法
void ClientConnectionManager::processCompleteMessage(int clientFd, const std::string& message) {
    std::cout << "[FD:" << clientFd << "] Processing message: " << message << std::endl;

    // 首先检查连接是否仍然有效
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        if (connections.find(clientFd) == connections.end()) {
            std::cout << "[FD:" << clientFd << "] Connection no longer exists, skipping message processing" << std::endl;
            return;
        }
    }

    try {
        json client_send_json = json::parse(message);
        std::string request_type = client_send_json.value("Type", "0");

        if (!request_type.empty()) {
            json response_client_Json = ResponseJsonToClient(client_send_json, clientFd);
            std::string responseStr = response_client_Json.dump();

            // 检查连接是否仍然有效再发送响应
            {
                std::lock_guard<std::mutex> lock(connectionMutex);
                if (connections.find(clientFd) != connections.end()) {
                    std::cout << "[FD:" << clientFd << "] Sending response: " << responseStr << std::endl;
                    sendResponse(clientFd, responseStr);
                } else {
                    std::cout << "[FD:" << clientFd << "] Connection closed, cannot send response" << std::endl;
                }
            }
        } else {
            std::lock_guard<std::mutex> lock(connectionMutex);
            if (connections.find(clientFd) != connections.end()) {
                sendResponse(clientFd, "Invalid message format");
            }
        }

    } catch (const json::exception& e) {
        std::cerr << "[FD:" << clientFd << "] JSON parse error: " << e.what() << std::endl;
        std::lock_guard<std::mutex> lock(connectionMutex);
        if (connections.find(clientFd) != connections.end()) {
            sendResponse(clientFd, "Invalid JSON");
        }
    }
}

void ClientConnectionManager::sendResponse(int clientFd, const std::string& response) {


    auto it = connections.find(clientFd);
    if (it == connections.end()) {
        std::cout << "[FD:" << clientFd << "] Connection not found when sending response" << std::endl;
        return;
    }

    auto& conn = it->second;


    // 检查连接状态
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(clientFd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        std::cout << "[FD:" << clientFd << "] Socket error detected, closing connection" << std::endl;
        closeConnection(clientFd);
        return;
    }

    std::lock_guard<std::mutex> writeLock(conn->writeMutex);
    std::cout << "[FD:" << clientFd << "] Preparing to send response: " << response << std::endl;

    // 准备发送数据
    uint32_t responseSize = htonl(static_cast<uint32_t>(response.size()));

    conn->writeBuffer.clear();
    conn->writeBuffer.resize(4 + response.size());

    memcpy(conn->writeBuffer.data(), &responseSize, 4);
    memcpy(conn->writeBuffer.data() + 4, response.c_str(), response.size());

    conn->bytesToWrite = conn->writeBuffer.size();
    conn->bytesWritten = 0;

    // 添加EPOLLOUT事件
    if (!modifyEpoll(clientFd, EPOLLIN | EPOLLOUT | EPOLLET)) {
        std::cerr << "[FD:" << clientFd << "] Failed to modify epoll for writing" << std::endl;
        // 尝试直接发送
        handleClientWrite(clientFd);
        return;
    }

    std::cout << "[FD:" << clientFd << "] Response queued for sending, size: "
              << conn->bytesToWrite << " bytes" << std::endl;
}


// 异步处理消息的方法
void ClientConnectionManager::processCompleteMessageAsync(int clientFd, const std::string& message) {
    // 在提交到线程池前，增加连接的引用计数或标记
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        if (connections.find(clientFd) == connections.end()) {
            std::cout << "[FD:" << clientFd << "] Connection not found, skipping async processing" << std::endl;
            return;
        }
    }

    // 提交到线程池异步处理，使用新的接口
    auto [submitted, future] = ThreadPool::getInstance().enqueue(
        [this, clientFd, message]() {
            try {
                this->processCompleteMessage(clientFd, message);
            } catch (const std::exception& e) {
                std::cerr << "[FD:" << clientFd << "] Exception in async message processing: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[FD:" << clientFd << "] Unknown exception in async message processing" << std::endl;
            }
        }
    );

    if (!submitted) {
        // 队列已满，在主线程中处理
        std::cout << "[FD:" << clientFd << "] 线程池队列已满，在主线程中处理消息" << std::endl;
        try {
            this->processCompleteMessage(clientFd, message);
        } catch (const std::exception& e) {
            std::cerr << "[FD:" << clientFd << "] 主线程处理异常: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[FD:" << clientFd << "] 主线程处理未知异常" << std::endl;
        }
    }
}


// 辅助方法实现
void ClientConnectionManager::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "fcntl F_GETFL failed: " << strerror(errno) << std::endl;
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "fcntl F_SETFL failed: " << strerror(errno) << std::endl;
    }
}

// 在实现文件中添加方法
void ClientConnectionManager::checkConnectionHealth(int clientFd) {
    int error = 0;
    socklen_t len = sizeof(error);

    if (getsockopt(clientFd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        std::cout << "[FD:" << clientFd << "] Failed to get socket error status" << std::endl;
        closeConnection(clientFd);
        return;
    }

    if (error != 0) {
        std::cout << "[FD:" << clientFd << "] Socket error detected: " << strerror(error) << std::endl;
        closeConnection(clientFd);
        return;
    }

    // 发送心跳包检测连接
    char heartbeat = 0;
    ssize_t result = send(clientFd, &heartbeat, 0, MSG_NOSIGNAL);
    if (result < 0) {
        int sendError = errno;
        if (sendError != EAGAIN && sendError != EWOULDBLOCK) {
            std::cout << "[FD:" << clientFd << "] Heartbeat failed: " << strerror(sendError) << std::endl;
            closeConnection(clientFd);
        }
    }
}

bool ClientConnectionManager::addToEpoll(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool ClientConnectionManager::modifyEpoll(int fd, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool ClientConnectionManager::removeFromEpoll(int fd) {
    return epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

std::string ClientConnectionManager::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm = *std::localtime(&now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 接收消息的分析和回复消息设置
nlohmann::json ClientConnectionManager::ResponseJsonToClient(const nlohmann::json &ReceivedJson, int clientsocket) {
    json response_to_client;
    std::string requestType = ReceivedJson.value("Type", "0");
    //登录请求
    if (requestType == "1") {
        std::unique_lock<std::mutex> sqllock(SqlMutex);
        bool loginSuccess = HandleLoginEvent(ReceivedJson, clientsocket);
        sqllock.unlock();
        if (loginSuccess) {
            std::lock_guard<std::mutex> lock(sessionMutex);

            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            if (getpeername(clientsocket, (struct sockaddr*)&clientAddr, &clientAddrLen) == -1) {
                std::cerr << "获取客户端地址失败: " << strerror(errno) << std::endl;
            } else {
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                uint16_t port = ntohs(clientAddr.sin_port);

                ClientSession session{
                    clientsocket,
                    ReceivedJson["Account"],
                    std::string(ipStr) + ":" + std::to_string(port),
                    time(nullptr)
                };

                accountToSession[ReceivedJson["Account"]] = session;
                socketToAccount[clientsocket] = ReceivedJson["Account"];
            }
        }

        response_to_client = {
            {"ResponseType", "OperationResult"},
            {"OperationType", 1},
            {"Success", loginSuccess ? "1" : "0"},
            {"Message", loginSuccess ? "操作成功" : "登录失败"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }
    //添加好友
    else if (requestType == "2") {
        std::unique_lock<std::mutex> sqllock(SqlMutex);
        bool addFriendSuccess = HandleAddFriendEvent(ReceivedJson, clientsocket);
        sqllock.unlock();

        response_to_client = {
            {"ResponseType", "OperationResult"},
            {"OperationType", 2},
            {"Success", addFriendSuccess ? "1" : "0"},
            {"Message", addFriendSuccess ? "添加成功" : "添加失败"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }
    //发送到好友消息 (转发消息)
    else if (requestType == "3") {
        int sendToFriendResult = HandleSendtoFriendEvent(ReceivedJson, clientsocket);
        response_to_client = {
            {"ResponseType", "OperationResult"},
            {"OperationType", 3},
            {"Success", sendToFriendResult? "1" : "0"},
            {"Message", sendToFriendResult ? "发送成功" : "发送失败"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }

    else {
        response_to_client = {
            {"ResponseType", "UnknownRequest"},
            {"Success", 0},
            {"Message", "未知请求类型"},
            {"Timestamp", getCurrentTimestamp()}
        };
    }

    std::cout << "Return Json: " << response_to_client.dump() << std::endl;
    return response_to_client;
}

bool ClientConnectionManager::HandleLoginEvent(const nlohmann::json &ReceivedJson, int clientsocket) {
    Sql_Manager &sqlManager = Sql_Manager::getInstance();
    return sqlManager.GoOnline(ReceivedJson["Account"], ReceivedJson["Content"]);
}

bool ClientConnectionManager::HandleAddFriendEvent(const nlohmann::json &ReceivedJson, int clientsocket) {
    Sql_Manager &sqlManager = Sql_Manager::getInstance();
    return sqlManager.AddFriend(ReceivedJson["Account"],ReceivedJson["Content"]);
}

int ClientConnectionManager::HandleSendtoFriendEvent(const nlohmann::json &ReceivedJson, int clientsocket) {
    std::unique_lock<std::mutex> sessionlock(sessionMutex);
    // 找到好友的session
    auto sessionIter = accountToSession.find(ReceivedJson["Target"]);
    if (sessionIter == accountToSession.end()) {
        return 0;  // 好友不存在
    }

    auto friendSession = sessionIter->second;
    int friendSocket = friendSession.sockfd;
    // 发送消息
    //构建发送json
    json sendToFriendJson = {
        {"ResponseType", "OperationResult"},
        {"OperationType", 4},
        {"Sender", ReceivedJson["Account"]},
        {"Content", ReceivedJson["Content"]},
        {"Timestamp", getCurrentTimestamp()}
    };

                // 发送消息
    std::string sendToFriendStr = sendToFriendJson.dump();
    uint32_t sendToFriendSize = htonl(static_cast<uint32_t>(sendToFriendStr.size()));
    for (int i = 0; i < 4; i++) {
        sendResponse(friendSocket, sendToFriendStr);
    }

    sendResponse(friendSocket, sendToFriendStr);
    //目前并没有返回是否成功, 后续需要增加返回值
    return 1;
}

