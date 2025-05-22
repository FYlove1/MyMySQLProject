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

int read_exact(int sockfd, void* buffer, size_t len) {
    char* ptr = static_cast<char*>(buffer);
    while (len > 0) {
        ssize_t bytes_read = recv(sockfd, ptr, len, 0);
        if (bytes_read <= 0) return -1;
        ptr += bytes_read;
        len -= bytes_read;
    }
    return 0;
}

int main() {
    ClientConnectionManager client_manager;
    client_manager.startServer(8080);
    
    client_manager.acceptConnections();
    // int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    //
    // struct sockaddr_in address;
    // address.sin_family = AF_INET;
    // address.sin_port = htons(8080);
    // address.sin_addr.s_addr = INADDR_ANY;
    //
    // if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
    //     std::cerr << "Bind failed\n";
    //     return -1;
    // }
    //
    // listen(server_socket,  5);
    //
    // while (true) {
    //     struct sockaddr_in client_addr;
    //     socklen_t client_len = sizeof(client_addr);
    //     int client_fd = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
    //     if (client_fd == -1) {
    //         std::cerr<<"Accept failed\n";
    //         continue;
    //     }
    //     uint32_t data_len;
    //     if (read_exact(client_fd, &data_len, sizeof(data_len)) == -1) {
    //         std::cerr<<"Read failed\n";
    //         close(client_fd);
    //         continue;
    //     }
    //     data_len = ntohl(data_len);
    //     std::string jsonstring(data_len,  '\0');
    //     if (read_exact(client_fd, &jsonstring[0], data_len)) {
    //         std::cerr<<"Read failed\n";
    //     }
    //     // 解析 JSON
    //     try {
    //         nlohmann::json j = nlohmann::json::parse(jsonstring);
    //         std::cout << "Received JSON: " << j.dump(4) << std::endl;
    //
    //         // 构造响应
    //         nlohmann::json response = {
    //             {"ResponseType", "OperationResult"},
    //             {"received", j},
    //                 {"Success","1"}
    //         };
    //
    //         std::string response_str = response.dump();
    //         uint32_t response_len = htonl(static_cast<uint32_t>(response_str.size()));
    //
    //         // 发送响应长度
    //         if (send(client_fd, &response_len, sizeof(response_len), 0) != sizeof(response_len)) {
    //             std::cerr << "Failed to send response length" << std::endl;
    //         }
    //
    //         // 发送响应数据
    //         if (send(client_fd, response_str.c_str(), response_str.size(), 0) != static_cast<ssize_t>(response_str.size())) {
    //             std::cerr << "Failed to send response data" << std::endl;
    //         }
    //         std::cout<<"Send Response Data Success"<<std::endl;
    //
    //     } catch (const nlohmann::json::parse_error& e) {
    //         std::cerr << "JSON parse error: " << e.what() << std::endl;
    //     }
    //
    //     close(client_fd);
    // }

    Sql_Manager& sql_manager = Sql_Manager::getInstance();


    // nlohmann::json json_object;
    // json_object["ceshi"] = 25;
    // std::string C_char = json_object.dump();
    // std::cout<<C_char<<std::endl;

    //bool testresult = sql_manager.AddFriend("111","222");
    //std::cout<<"testresult is :"<<testresult<<std::endl;
    // sql_manager.AddNewUser("222",  "laosan", "123456");

    //bool is_online =sql_manager.IfUserOnline("1655555");
    //std::cout<<"if Online"<<is_online<<std::endl;
    return 0;
}