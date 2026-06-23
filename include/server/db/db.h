#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>
using namespace std;

// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    // 连接数据库（优先从连接池获取）
    bool connect();
    // 更新操作
    bool update(string sql);

    // 查询操作
    MYSQL_RES *query(string sql);

    //获取连接
    MYSQL* getConnection();

    // SQL 字符串转义（防注入）
    string escape(const string &str);

private:
    MYSQL *_conn;
    bool _fromPool;  // 标记连接是否来自连接池
};

#endif
