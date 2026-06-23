#include "usermodel.hpp"
#include "db.h"
#include <muduo/base/Logging.h>
#include <iostream>
using namespace std;

// user表: id(int,PRI,auto_increment) name(varchar(50),UNI) pwd(varchar(50)) state(int,default 0)
// 代码中state用string: "offline"/"online"，数据库用int: 0/1，必须做映射

bool UserModel::insert(User &user)
{
    int state = (user.getState() == "online") ? 1 : 0;

    MySQL mysql;
    if (!mysql.connect()) return false;

    char sql[1024] = {0};
    sprintf(sql, "insert into user(name, pwd, state) values('%s', '%s', %d)",
        mysql.escape(user.getName()).c_str(),
        mysql.escape(user.getPwd()).c_str(),
        state);

    if (mysql.update(sql))
    {
        user.setId(mysql_insert_id(mysql.getConnection()));
        return true;
    }
    return false;
}

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
                // ★ 数据库state是int(0=离线,1=在线)，映射为string
                user.setState(atoi(row[3]) == 1 ? "online" : "offline");
                mysql_free_result(res);
                return user;
            }
        }
    }
    return User();
}

bool UserModel::updateState(User user)
{
    int state = (user.getState() == "online") ? 1 : 0;

    char sql[1024] = {0};
    sprintf(sql, "update user set state = %d where id = %d", state, user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

void UserModel::resetState()
{
    char sql[1024] = "update user set state = 0 where state = 1";
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

bool UserModel::isUserExist(string name)
{
    MySQL mysql;
    if (!mysql.connect()) return false;

    char sql[1024] = {0};
    sprintf(sql, "select * from user where name = '%s'", mysql.escape(name).c_str());

    MYSQL_RES *res = mysql.query(sql);
    if (res != nullptr)
    {
        int rowCount = mysql_num_rows(res);
        mysql_free_result(res);
        return rowCount > 0;
    }
    return false;
}
