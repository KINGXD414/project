# 集群聊天服务器

## 项目介绍

基于 C++ Muduo 网络库实现的**高并发、分布式、可集群部署**的聊天服务器，支持用户注册登录、一对一聊天、好友管理、群组管理、离线消息、跨服务器消息转发等完整功能。

### 架构概览

```
                         +-----------------------+
                         |    Nginx :8000         |
                         |  TCP Stream Proxy      |
                         |  least_conn + 健康检查   |
                         +----+------+------+-----+
                              |      |      |
                +-------------+------+------+-------------+
                |             |      |      |             |
                v             v      v      v             v
         +------------+ +------------+ +------------+
         |ChatServer  | |ChatServer  | |ChatServer  |   <-- Muduo Reactor
         | :6000      | | :6001      | | :6002      |       对等集群
         +------+-----+ +------+-----+ +------+-----+
                |              |              |
                +--------------+--------------+
                               |
                +--------------+--------------+
                |                             |
                v                             v
         +--------------+          +------------------+
         |    MySQL     |          |     Redis        |
         |  持久化+消息  |          | pub/sub 跨节点通信 |
         |  兜底可靠性   |          | + Cache-Aside 缓存 |
         +--------------+          +------------------+
```

### 消息可靠性: Store-then-Deliver

每条聊天消息采用 **先落库、再推送** 策略，保证 at-least-once 语义：

```
Sender 发消息
    |
    v
1. 生成 msg_uuid -> 存入 offlineMsg 表 (落库兜底)
    |
    v
2. 尝试实时投递 (本机 TcpConnection 或 Redis pub/sub 跨节点)
    |
    v
3. 客户端收到 -> 回 ACK (含 msg_uuid)
    |
    v
4. 服务端收到 ACK -> 删除 offlineMsg 对应记录
```

- **正常路径**: 消息实时送达 -> ACK -> 清理 -> 无残留
- **异常路径**: 服务器宕机/网络断开 -> 消息留在 DB -> 用户下次登录批量拉取
- **去重**: 登录拉取后全量删除; ACK 已清理的消息不会重复出现

## 技术栈

* C++11
* Muduo Reactor 高并发网络库
* MySQL 8.0 数据持久化 + 连接池
* Redis 发布订阅（跨服务器通信）+ Cache-Aside 缓存
* Nginx TCP Stream 四层负载均衡
* nlohmann json 序列化
* CMake 构建
* 单例模式、线程安全、异常处理

## 功能列表

* [x] 用户注册
* [x] 用户登录/退出
* [x] 一对一聊天
* [x] 好友添加/查询
* [x] 创建群组/加入群组
* [x] 群组聊天
* [x] 离线消息存储与推送
* [x] 多服务器集群部署
* [x] 跨服务器消息转发（Redis pub/sub）
* [x] 服务器异常下线自动处理
* [x] Nginx TCP Stream 四层负载均衡 + 健康检查
* [x] Store-then-Deliver 消息可靠性（at-least-once）
* [x] 客户端 ACK 确认机制

## 目录结构

```
.
├── nginx.conf                 # Nginx TCP Stream 负载均衡配置
├── CMakeLists.txt
├── autobuild.sh
├── run_all_tests.sh           # 全维度压测（登录/注册/聊天/浸泡）
├── soak_test.sh               # 5分钟稳定性浸泡测试
├── cluster.sh                 # 集群管理（启动/停止/扩缩容）
├── bin/                       # 编译产物
│   ├── ChatClient
│   ├── ChatServer
│   ├── ChatBench
│   └── ConnBench
├── include/
│   ├── public.hpp              # 消息类型枚举 + msg_uuid 生成
│   ├── protocol.hpp            # 长度前缀帧协议
│   └── server/
│       ├── chatserver.hpp
│       ├── chatservice.hpp
│       ├── db/
│       │   ├── db.h
│       │   └── connectionpool.hpp
│       ├── model/
│       │   ├── user.hpp
│       │   ├── usermodel.hpp
│       │   ├── friendmodel.hpp
│       │   ├── group.hpp
│       │   ├── groupuser.hpp
│       │   ├── groupmodel.hpp
│       │   └── offlinemessagemodel.hpp
│       ├── redis/
│       │   └── redis.hpp
│       └── threadpool/
│           ├── FixedThreadPool.hpp
│           └── syncQueue_2.hpp
├── src/
│   ├── client/
│   │   └── main.cpp           # 客户端（含ACK逻辑）
│   └── server/
│       ├── main.cpp            # 服务端入口
│       ├── chatserver.cpp      # Muduo TCP Server
│       ├── chatservice.cpp     # 业务逻辑核心（Store-then-Deliver）
│       ├── db/
│       │   └── db.cpp
│       ├── model/
│       │   ├── usermodel.cpp
│       │   ├── friendmodel.cpp
│       │   ├── groupmodel.cpp
│       │   └── offlinemessagemodel.cpp
│       └── redis/
│           └── redis.cpp
├── example/                   # 压测工具
│   ├── bench_main.cpp
│   ├── bench_worker.hpp       # 5种压测场景
│   ├── bench_stats.hpp        # 分位数统计
│   └── conn_bench.cpp         # TCP连接数压测
└── thirdparty/
    └── json.hpp               # nlohmann/json (header-only)
```

## 编译运行

### 1. 编译

```bash
chmod +x autobuild.sh
./autobuild.sh
```

### 2. 启动后端集群

```bash
# 方式A: 单实例（开发调试）
cd bin
./ChatServer 127.0.0.1 6000

# 方式B: 集群管理脚本（推荐）
chmod +x cluster.sh
./cluster.sh start        # 启动 3 节点 + 自动更新 Nginx
./cluster.sh start 5      # 启动 5 节点
./cluster.sh start 3 7000 # 3 节点，从 7000 端口开始
./cluster.sh status       # 查看集群状态
./cluster.sh add 6005     # 热添加节点（自动更新 Nginx upstream）
./cluster.sh del 6001     # 热移除节点（自动更新 Nginx upstream）
./cluster.sh stop         # 停止全部

# 方式C: 手动启动（不依赖 Nginx）
./ChatServer 0.0.0.0 6000 &
./ChatServer 0.0.0.0 6001 &
./ChatServer 0.0.0.0 6002 &
```

### 3. 启动 Nginx 负载均衡（可选）

```bash
# 安装 Nginx（需支持 stream 模块，1.9.0+）
sudo apt install nginx

# 使用项目配置
sudo nginx -c /path/to/MuduoChatServer/nginx.conf

# 或追加到系统配置
# sudo cp nginx.conf /etc/nginx/stream.conf
# 在 /etc/nginx/nginx.conf 中: include /etc/nginx/stream.conf;
```

### 4. 客户端连接

```bash
# 直连单实例
./ChatClient 127.0.0.1 6000

# 通过 Nginx 负载均衡（推荐）
./ChatClient 127.0.0.1 8000
```

### 5. 压测

```bash
# 全维度压测（登录梯度/注册/聊天/线程池A/B/浸泡）
./run_all_tests.sh

# 单独浸泡测试（5分钟，检测内存/句柄泄露）
./soak_test.sh

# ChatBench 命令行（手动调参）
./bin/ChatBench --ip 127.0.0.1 --port 6000 --scenario login --concurrency 100 --total 500
```

## 核心设计决策

| 层面 | 决策 | 原因 |
|------|------|------|
| 协议 | 长度前缀帧 (4字节大端头+载荷) | O(1) 粘包处理，替代分隔符方案 |
| 并发 | IO线程 + 业务线程池分离 | dispatch 模式，避免 IO 线程阻塞 |
| 缓存 | Redis Cache-Aside | user/friends/groups 三类热点数据缓存 |
| 集群 | Redis pub/sub 对等节点 | 跨服务器消息实时路由 |
| 可靠性 | Store-then-Deliver | 先落 offlineMsg 再推送，DB 兜底 |
| 负载均衡 | Nginx TCP Stream | 单入口 + least_conn + 故障转移 |

## 数据库

```sql
-- 创建数据库
CREATE DATABASE chat DEFAULT CHARACTER SET utf8mb4;

-- 用户表
CREATE TABLE user (
    id INT PRIMARY KEY AUTO_INCREMENT,
    name VARCHAR(50) UNIQUE NOT NULL,
    pwd VARCHAR(50) NOT NULL,
    state INT DEFAULT 0  -- 0=离线, 1=在线
);

-- 好友表 (双向复合主键)
CREATE TABLE friend (
    userid INT,
    friendid INT,
    PRIMARY KEY (userid, friendid)
);

-- 群组表
CREATE TABLE `group` (
    id INT PRIMARY KEY AUTO_INCREMENT,
    groupname VARCHAR(50) NOT NULL,
    groupdesc VARCHAR(200) DEFAULT ''
);

-- 群组成员表
CREATE TABLE groupuser (
    id INT PRIMARY KEY AUTO_INCREMENT,
    groupid INT,
    userid INT,
    grouprole VARCHAR(20) DEFAULT 'member'
);

-- 离线消息表 (Store-then-Deliver 兜底存储)
CREATE TABLE offlinemessage (
    userid INT,
    message VARCHAR(2000)  -- 含 msg_uuid 的完整 JSON
);
```
