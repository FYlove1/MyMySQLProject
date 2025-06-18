//
// Created by fylove on 25-5-14.
//

#include "Sql_Manager.h"

#include <iostream>
#include <mysql/mysql.h>
#include "Sql_Manager.h"
//#include <cppconn/driver.h>

#include <cstring>



Sql_Manager& Sql_Manager::getInstance() {
    static Sql_Manager instance; // 局部静态变量，线程安全且自动释放
    return instance;
}

Sql_Manager::Sql_Manager() {
    conn = mysql_init(nullptr);
    if (!mysql_real_connect(conn, "localhost", "testuser", "123456789", "ChatServer", 3306, nullptr, 0)) {
        // 错误处理
        std::cerr << "Failed to connect to MySQL server: " << mysql_error(conn) << std::endl;
    }
}

Sql_Manager::~Sql_Manager() {
    if (conn != nullptr) {
        mysql_close(conn);
        conn = nullptr;
    }
}

//add new User
bool Sql_Manager::AddNewUser(const std::string& Account, const std::string& UserName, const std::string& passwd) {
    std::lock_guard<std::mutex> lock(mtx);

    std::string sql = "INSERT INTO User_Information (Account, UserName, UserPassword) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = prepareStatement(sql);
    if (!stmt) return false;

    MYSQL_BIND bind[3];
    std::memset(bind, 0, sizeof(bind));

    // 绑定参数
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)UserName.c_str();
    bind[1].buffer_length = UserName.length();

    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (void*)passwd.c_str();
    bind[2].buffer_length = passwd.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "Binding parameters failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        std::cerr << "Execution failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}


bool Sql_Manager::IfUserOnline(const std::string &Account) const {
    std::lock_guard<std::mutex> lock(mtx);

    std::string sql = "SELECT UserStatus FROM User_Information WHERE Account = ?";
    MYSQL_STMT* stmt = prepareStatement(sql);
    if (!stmt) return false;

    // 绑定参数
    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "Binding parameters failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 执行查询
    if (mysql_stmt_execute(stmt)) {
        std::cerr << "Execution failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 接收结果
    int userStatusValue = 0;
    bool is_null = false;

    MYSQL_BIND result_bind;
    std::memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer_type = MYSQL_TYPE_TINY; // 对应 TINYINT(1)

    result_bind.buffer = &userStatusValue;
    result_bind.is_null = &is_null;

    if (mysql_stmt_bind_result(stmt, &result_bind)) {
        std::cerr << "Binding result failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_fetch(stmt)) {
        mysql_stmt_close(stmt);
        return false; // 查询失败或无数据
    }

    mysql_stmt_close(stmt);

    return userStatusValue == 1; // 返回 true 表示在线
}

bool Sql_Manager::AddFriend(const std::string &Account, const std::string &FriendAccount) const {
    std::lock_guard<std::mutex> lock(mtx);

    // 添加双向好友关系
    std::string sql = "INSERT INTO UserFriend (Account, FriendAccount) VALUES (?, ?), (?, ?)";
    MYSQL_STMT* stmt = prepareStatement(sql);
    if (!stmt) return false;

    MYSQL_BIND bind[4];
    std::memset(bind, 0, sizeof(bind));

    // 第一组：Account -> FriendAccount
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (void*)FriendAccount.c_str();
    bind[1].buffer_length = FriendAccount.length();

    // 第二组：FriendAccount -> Account
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (void*)FriendAccount.c_str();
    bind[2].buffer_length = FriendAccount.length();

    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (void*)Account.c_str();
    bind[3].buffer_length = Account.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "Binding parameters failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        std::cerr << "Execution failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}

bool Sql_Manager::GoOnline(const std::string &Account, const std::string &PassWord) {
    if (IfUserOnline(Account)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mtx);

    // 查询用户密码
    std::string sql = "SELECT UserPassword FROM User_Information WHERE Account = ?";
    MYSQL_STMT* stmt = prepareStatement(sql);
    if (!stmt) return false;

    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "Binding parameters failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        std::cerr << "Execution failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    char password[256]; // 假设密码长度不超过256
    unsigned long length = 0;
    bool is_null = false;

    MYSQL_BIND result_bind;
    std::memset(&result_bind, 0, sizeof(result_bind));
    result_bind.buffer = password;
    result_bind.buffer_length = sizeof(password);
    result_bind.length = &length;
    result_bind.is_null = &is_null;
    result_bind.buffer_type = MYSQL_TYPE_STRING;

    if (mysql_stmt_bind_result(stmt, &result_bind)) {
        std::cerr << "Binding result failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_fetch(stmt)) {
        mysql_stmt_close(stmt);
        return false; // 没有查到用户
    }

    mysql_stmt_close(stmt);

    if (is_null || std::string(password, length) != PassWord) {
        return false; // 密码不匹配
    }

    // 更新用户状态为在线
    sql = "UPDATE User_Information SET UserStatus = 1 WHERE Account = ?";
    stmt = prepareStatement(sql);
    if (!stmt) return false;

    std::memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "Binding parameters failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        std::cerr << "Execution failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}

bool Sql_Manager::GoOffline(const std::string &Account) {
    std::lock_guard<std::mutex> lock(mtx); // 保证线程安全

    // 准备更新用户状态的SQL语句
    std::string sql = "UPDATE User_Information SET UserStatus = 0 WHERE Account = ?";
    MYSQL_STMT* stmt = prepareStatement(sql);
    if (!stmt) {
        std::cerr << "准备下线SQL语句失败" << std::endl;
        return false;
    }

    // 绑定账户参数
    MYSQL_BIND bind[1];
    std::memset(bind, 0, sizeof(bind));

    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = (void*)Account.c_str();
    bind[0].buffer_length = Account.length();

    // 执行参数绑定
    if (mysql_stmt_bind_param(stmt, bind)) {
        std::cerr << "参数绑定失败: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 执行更新操作
    if (mysql_stmt_execute(stmt)) {
        std::cerr << "执行下线操作失败: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return false;
    }

    // 释放资源
    mysql_stmt_close(stmt);
    return true;
}


// 封装预处理语句初始化
MYSQL_STMT* Sql_Manager::prepareStatement(const std::string& sql) const {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) {
        std::cerr << "mysql_stmt_init() failed" << std::endl;
        return nullptr;
    }

    if (mysql_stmt_prepare(stmt, sql.c_str(), sql.length())) {
        std::cerr << "mysql_stmt_prepare() failed: " << mysql_stmt_error(stmt) << std::endl;
        mysql_stmt_close(stmt);
        return nullptr;
    }

    return stmt;
}

