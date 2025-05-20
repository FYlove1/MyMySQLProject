#include <mysql/mysql.h>
#include <iostream>
#include <string>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/exception.h>
#include "Sql_Manager.h"
int main() {
    Sql_Manager& sql_manager = Sql_Manager::getInstance();
    //sql_manager.AddNewUser("1655555",  "张三", "123456");

    bool is_online =sql_manager.IfUserOnline("1655555");
    std::cout<<"if Online"<<is_online<<std::endl;
    return 0;
}