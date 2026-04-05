#include "usermodel.hpp"
#include "db.h"
#include <muduo/base/Logging.h>
#include <iostream>
using namespace std;

// User表的增加方法
// User表的增加方法
bool UserModel::insert(User &user)
{
    // 状态映射：offline → 0，online → 1
    int state = 0;
    if (user.getState() == "online")
        state = 1;

    // 正确SQL
    char sql[1024] = {0};
    sprintf(sql, "insert into user(name, pwd, state) values('%s', '%s', %d)",
        user.getName().c_str(),
        user.getPwd().c_str(),
        state);

    MySQL mysql;
    if (mysql.connect())
    {
        bool ret = mysql.update(sql);
        if (ret)
        {
            user.setId(mysql_insert_id(mysql.getConnection()));
        }
        return ret;
    }
    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                return user;
            }
        }
    }
    return User();
}

// 更新用户的状态信息 
bool UserModel::updateState(User user)
{
    int state = 0;
    if (user.getState() == "online")
        state = 1;

    char sql[1024] = {0};
    sprintf(sql, "update user set state = %d where id = %d", state, user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

// 重置用户的状态信息 
void UserModel::resetState()
{
    char sql[1024] = "update user set state = 0 where state = 1";
    
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 检查用户名是否已经存在
bool UserModel::isUserExist(string name)
{
    char sql[1024] = {0};
    sprintf(sql, "select * from user where name = '%s'", name.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            int rowCount = mysql_num_rows(res);
            mysql_free_result(res);
            return rowCount > 0;
        }
    }
    return false;
}
