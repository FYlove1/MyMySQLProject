//
// Created by fylove on 25-5-14.
//

#ifndef SQL_MANAGER_H
#define SQL_MANAGER_H
#include <mutex>
#include <mysql/mysql.h>
#include <string>


class Sql_Manager {
public:
    static Sql_Manager& getInstance(); // 返回引用
    bool AddNewUser(const std::string &Account, const std::string &UserName, const std::string &passwd);
    bool IfUserOnline(const std::string &Account) const;
    bool AddFriend(const std::string &Account, const std::string &FriendAccount) const;
    bool GoOnline(const std::string &Account,const std::string &PassWord);

private:
    Sql_Manager();
    ~Sql_Manager();

    MYSQL_STMT *prepareStatement(const std::string &sql) const;

    Sql_Manager(const Sql_Manager&) = delete;
    Sql_Manager& operator=(const Sql_Manager&) = delete;

    MYSQL* conn;
    mutable std::mutex mtx; //互斥锁
};




#endif //SQL_MANAGER_H
