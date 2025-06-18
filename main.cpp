#include <mysql/mysql.h>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>
#include "Sql_Manager.h"
#include "ClientConnectionManager.h"
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <csignal>

// 全局标志，用于退出
volatile bool g_running = true;

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    // 注册信号处理器，用于优雅退出
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // 初始化数据库管理器
    Sql_Manager& sql_manager = Sql_Manager::getInstance();

    //启动线程池


    // 创建并启动服务器
    ClientConnectionManager client_manager;

    if (!client_manager.startServer(8080)) {
        std::cerr << "Failed to start server" << std::endl;
        return -1;
    }

    std::cout << "Server is running. Press Ctrl+C to stop." << std::endl;

    // 主事件循环
    while (g_running && client_manager.isRunning()) {
        // 处理网络事件，超时时间100毫秒
        int events_processed = client_manager.processEvents(1000);

        if (events_processed == -1) {
            std::cerr << "Error processing events, shutting down..." << std::endl;
            break;
        }

        // 可以在这里添加其他定期任务
        // 例如：清理过期连接、发送心跳包等

        // 如果处理了事件，可以打印一些统计信息
        if (events_processed > 0) {
            // std::cout << "Processed " << events_processed << " events" << std::endl;
        }
    }

    // 停止服务器
    client_manager.stopServer();
    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}