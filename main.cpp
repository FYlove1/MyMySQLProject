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

    while (true){}



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