#include "groupmodel.hpp"
#include "db.h"
#include <muduo/base/Logging.h>

// group表: id(int,PRI,auto_increment) groupname(varchar(50)) groupdesc(varchar(200))
// groupuser表: id(int,PRI,auto_increment) groupid(int) userid(int) grouprole(varchar(20),default "member")

bool GroupModel::creatGroup(Group &group)
{
    MySQL mysql;
    if (!mysql.connect()) return false;

    char sql[1024] = {0};
    sprintf(sql, "insert into `group`(groupname, groupdesc) values('%s', '%s')",
            mysql.escape(group.getName()).c_str(),
            mysql.escape(group.getDesc()).c_str());

    if (mysql.update(sql))
    {
        // 获取自增的群ID
        group.setId(mysql_insert_id(mysql.getConnection()));
        return true;
    }
    return false;
}

void GroupModel::addGroup(int userid, int groupid, string role)
{
    MySQL mysql;
    if (!mysql.connect()) return;

    char sql[1024] = {0};
    sprintf(sql, "insert into groupuser(groupid, userid, grouprole) values(%d, %d, '%s')",
            groupid, userid, mysql.escape(role).c_str());

    mysql.update(sql);
}

// ★ 查询用户所在群组，必须包含群组成员列表（含role）
// 客户端doLoginResponse期望每个group有 "users" 字段，每个user有 id/name/state/role
vector<Group> GroupModel::queryGroups(int userid)
{
    // 1. 查用户所在的所有群组
    char sql[1024] = {0};
    sprintf(sql,
        "select a.id, a.groupname, a.groupdesc from `group` a "
        "inner join groupuser b on a.id = b.groupid where b.userid = %d",
        userid);

    vector<Group> groupVec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                groupVec.push_back(group);
            }
            mysql_free_result(res);
        }
    }

    // 2. 对每个群组，查询其所有成员（join user+groupuser，拿到id/name/state/role）
    for (Group &group : groupVec)
    {
        char sql2[1024] = {0};
        sprintf(sql2,
            "select a.id, a.name, a.state, b.grouprole from user a "
            "inner join groupuser b on a.id = b.userid where b.groupid = %d",
            group.getId());

        if (mysql.connect())
        {
            MYSQL_RES *res = mysql.query(sql2);
            if (res != nullptr)
            {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res)) != nullptr)
                {
                    GroupUser user;
                    user.setId(atoi(row[0]));
                    user.setName(row[1]);
                    // ★ 数据库state是int，映射为string
                    user.setState(atoi(row[2]) == 1 ? "online" : "offline");
                    user.setRole(row[3]);
                    group.getUsers().push_back(user);
                }
                mysql_free_result(res);
            }
        }
    }

    return groupVec;
}

vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d",
            groupid, userid);

    vector<int> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return vec;
}
