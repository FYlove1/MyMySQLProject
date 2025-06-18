cmake_minimum_required(VERSION 3.30)
# 设置 C++ 标准为 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)




# 项目名称和版本
project(MyMySQLProject VERSION 1.0)

# 设置 C++ 标准（根据需求选择 C++11/14/17/20）

# 是否构建示例程序（可选）
set(BUILD_EXAMPLES OFF CACHE BOOL "是否构建示例程序")

# 设置 MySQL 安装路径（需根据实际路径修改）
# 默认值：系统自带路径（如 /usr/lib/x86_64-linux-gnu）
set(MYSQL_DIR "/usr/local/mysql" CACHE PATH "MySQL 安装路径")
set(MYSQL_LIB_DIR "${MYSQL_DIR}/lib" CACHE PATH "MySQL 库文件路径")

# 是否静态链接 MySQL 客户端库（推荐 OFF，除非你有静态库）
# ON: 静态链接（需要 libmysqlclient.a）
# OFF: 动态链接（需要 libmysqlclient.so）
set(MYSQLCLIENT_STATIC_BINDING OFF CACHE BOOL "是否静态链接 MySQL 客户端库")

# 查找 MySQL 客户端库
find_library(MYSQL_LIB
        NAMES
        mysqlclient  # 动态库名称（Ubuntu 默认为 libmysqlclient.so）
        mysqlclient_r
        libmysqlclient.so
        libmysqlclient_r.so
        PATHS
        ${MYSQL_LIB_DIR}
        ${MYSQL_LIB_DIR}/libmysql_r/.libs
        ${MYSQL_LIB_DIR}/lib
        ${MYSQL_LIB_DIR}/lib/mysql
        $ENV{MYSQL_LIB_DIR}
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
        /usr/lib/i386-linux-gnu
        /usr/lib/mysql
        /usr/local/lib
        /usr/local/mysql/lib
        NO_DEFAULT_PATH
)

# 查找 MySQL 头文件
find_path(MYSQL_INCLUDE_DIR
        mysql.h
        PATHS
        ${MYSQL_DIR}/include
        ${MYSQL_DIR}/include/mysql
        $ENV{MYSQL_DIR}/include
        $ENV{MYSQL_DIR}/include/mysql
        /usr/include/mysql
        /usr/local/include/mysql
        /usr/include/x86_64-linux-gnu/mysql
        /usr/include/aarch64-linux-gnu/mysql
        NO_DEFAULT_PATH
)

# 检查是否找到 MySQL 库和头文件
if(NOT MYSQL_LIB OR NOT MYSQL_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 MySQL 客户端库或头文件，请检查安装路径或依赖是否安装")
endif()

# 添加包含目录
include_directories(${MYSQL_INCLUDE_DIR})



set(SOURCES main.cpp
        Sql_Manager.cpp
        Sql_Manager.h
        ClientConnectionManager.cpp
        ClientConnectionManager.h
        ThreadPool.cpp
        ThreadPool.h)
# 添加可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接 MySQL 库
target_link_libraries(${PROJECT_NAME} ${MYSQL_LIB})

# 如果需要静态链接 MySQL 客户端库
if(MYSQLCLIENT_STATIC_BINDING)
    # 示例：静态链接 libmysqlclient.a（需确认文件存在）
    find_library(MYSQL_STATIC_LIB
            NAMES
            mysqlclient.a
            libmysqlclient.a
            PATHS
            ${MYSQL_LIB_DIR}
            ${MYSQL_LIB_DIR}/libmysql_r/.libs
            ${MYSQL_LIB_DIR}/lib
            ${MYSQL_LIB_DIR}/lib/mysql
            NO_DEFAULT_PATH
    )
    if(MYSQL_STATIC_LIB)
        target_link_libraries(${PROJECT_NAME} ${MYSQL_STATIC_LIB})
    else()
        message(WARNING "未找到静态 MySQL 客户端库，强制切换为动态链接")
    endif()
endif()

# 可选：构建示例程序
if(BUILD_EXAMPLES)
    add_executable(example example.cpp)
    target_link_libraries(example ${MYSQL_LIB})
    if(MYSQLCLIENT_STATIC_BINDING AND MYSQL_STATIC_LIB)
        target_link_libraries(example ${MYSQL_STATIC_LIB})
    endif()
endif()

# 输出配置信息
message(STATUS "MySQL 客户端库路径: ${MYSQL_LIB}")
message(STATUS "MySQL 头文件路径: ${MYSQL_INCLUDE_DIR}")


include_directories(/usr/include/mysql)
include_directories(/usr/include/mysqlx)
# 查找MySQL Connector/C++包
#ind_package(mysqlcppconn REQUIRED)



