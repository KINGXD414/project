#include "friendmodel.hpp"
#include "db.h"
#include <muduo/base/Logging.h>

// friend表: userid(int,PRI) friendid(int,PRI) 复合主键，没有auto_increment

void FriendModel::insert(int userid, int friendid)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        LOG_ERROR << "FriendModel::insert connect mysql failed";
        return;
    }

    // INSERT IGNORE: 已存在则跳过，避免主键冲突报错
    // 双向插入：A加B，B也能看到A
    char sql[1024] = {0};
    sprintf(sql, "INSERT IGNORE INTO friend(userid, friendid) VALUES(%d, %d), (%d, %d)",
            userid, friendid, friendid, userid);
    if (!mysql.update(sql))
    {
        LOG_ERROR << "FriendModel::insert failed: " << sql;
    }
}

vector<User> FriendModel::query(int userid)
{
    // join friend+user，查好友的id/name/state
    char sql[1024] = {0};
    sprintf(sql,
        "select a.id, a.name, a.state from user a inner join friend b on b.friendid = a.id where b.userid = %d",
        userid);

    vector<User> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                // ★ 数据库state是int，映射为string
                user.setState(atoi(row[2]) == 1 ? "online" : "offline");
                vec.push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return vec;
}
