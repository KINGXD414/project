#ifndef BENCH_WORKER_HPP
#define BENCH_WORKER_HPP

#include "json.hpp"
#include "protocol.hpp"
#include "public.hpp"
#include "bench_stats.hpp"
#include <string>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <iostream>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

using json = nlohmann::json;

namespace bench {

struct WorkerConfig {
    std::string ip = "127.0.0.1";
    int port = 6000;
    std::string scenario = "login";
    int connect_timeout_ms = 3000;   // 连接超时
    int recv_timeout_ms = 5000;       // 接收超时
    int total_requests = 1000;        // 总请求数（对mixed场景）
    int chat_repeat = 1;              // chat场景每条连接发多少条消息
    int user_base_id = 1001;          // 登录/聊天场景的用户ID起始值
    int user_pool_size = 500;         // 可用测试用户数量（超出时循环复用）
};

// ============================================================
// 工具函数
// ============================================================
inline std::string buildRequest(const json &msg) {
    // 长度前缀帧协议: 4字节头(大端) + JSON消息体
    std::string payload = msg.dump();
    uint32_t netLen = htonl(static_cast<uint32_t>(payload.size()));
    std::string framed;
    framed.reserve(4 + payload.size());
    framed.append(reinterpret_cast<const char*>(&netLen), 4);
    framed.append(payload);
    return framed;
}

inline void setSocketTimeout(int fd, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// ============================================================
// 通信基础：连接 → 发请求 → 收响应
// ============================================================
inline int createConnection(const std::string &ip, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    // 设置 connect 超时：先设为非阻塞，connect 后再设回阻塞
    setSocketTimeout(fd, timeout_ms);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // 设置收/发超时
    setSocketTimeout(fd, timeout_ms);
    return fd;
}

// 发送请求 + 接收响应，返回 (success, latency_ms)
inline std::pair<bool, double> sendAndRecv(int fd, const std::string &request, std::string &response, int recv_timeout_ms) {
    auto t0 = std::chrono::steady_clock::now();

    // 发送（带长度前缀的完整帧）
    ssize_t sent = sendAll(fd, request.data(), request.size());
    if (sent <= 0) {
        return {false, 0};
    }

    // 接收：读4字节长度头 → 读len字节消息体
    uint32_t netLen = 0;
    if (recvAll(fd, &netLen, 4) <= 0) {
        return {false, 0};
    }
    uint32_t bodyLen = ntohl(netLen);
    if (bodyLen == 0 || bodyLen > kMaxMessageSize) {
        return {false, 0};
    }
    response.resize(bodyLen);
    if (recvAll(fd, &response[0], bodyLen) <= 0) {
        return {false, 0};
    }

    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {true, ms};
}

// ============================================================
// 各场景的 Worker 函数
// ============================================================

// 场景1：注册 —— 每次新建连接
void runRegisterBatch(const WorkerConfig &cfg, BenchStats &stats, int batch_start_id, int batch_size) {
    for (int i = 0; i < batch_size; i++) {
        int uid = batch_start_id + i;
        int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
        if (fd < 0) {
            stats.recordFailure("connect_failed");
            continue;
        }

        json msg;
        msg["msgid"] = REG_MSG;
        msg["name"] = "bench_user_" + std::to_string(uid);
        msg["password"] = "bench123";

        std::string resp;
        auto [ok, lat] = sendAndRecv(fd, buildRequest(msg), resp, cfg.recv_timeout_ms);

        if (ok && !resp.empty()) {
            try {
                json r = json::parse(resp);
                if (r.value("errno", -1) == 0) {
                    stats.recordSuccess(lat);
                } else {
                    stats.recordFailure("reg_business_fail");
                }
            } catch (...) {
                stats.recordFailure("reg_parse_error");
            }
        } else {
            stats.recordFailure("reg_recv_timeout");
        }
        close(fd);
    }
}

// 场景2：登录 —— 每个任务新建连接，登录后断开
void runLoginBatch(const WorkerConfig &cfg, BenchStats &stats, int batch_start_id, int batch_size) {
    for (int i = 0; i < batch_size; i++) {
        // 用已存在的用户ID登录（超出pool_size时循环复用）
        int userid = cfg.user_base_id + (batch_start_id + i) % cfg.user_pool_size;

        int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
        if (fd < 0) {
            stats.recordFailure("connect_failed");
            continue;
        }

        json msg;
        msg["msgid"] = LOGIN_MSG;
        msg["id"] = userid;
        msg["password"] = "bench123";

        std::string resp;
        auto [ok, lat] = sendAndRecv(fd, buildRequest(msg), resp, cfg.recv_timeout_ms);

        if (ok && !resp.empty()) {
            try {
                json r = json::parse(resp);
                if (r.value("errno", -1) == 0) {
                    stats.recordSuccess(lat);
                } else {
                    stats.recordFailure("login_business_fail");
                }
            } catch (...) {
                stats.recordFailure("login_parse_error");
            }
        } else {
            stats.recordFailure("login_recv_timeout");
        }
        close(fd);
    }
}

// 场景3：一对一聊天 —— 登录后发消息，测试消息处理吞吐
void runChatBatch(const WorkerConfig &cfg, BenchStats &stats, int batch_start_id, int batch_size) {
    for (int i = 0; i < batch_size; i++) {
        int userid = cfg.user_base_id + (batch_start_id + i) % cfg.user_pool_size;
        int target_id = cfg.user_base_id + ((batch_start_id + i + 1) % cfg.user_pool_size);

        int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
        if (fd < 0) {
            stats.recordFailure("connect_failed");
            continue;
        }

        // 1. 登录
        json loginMsg;
        loginMsg["msgid"] = LOGIN_MSG;
        loginMsg["id"] = userid;
        loginMsg["password"] = "bench123";

        std::string resp;
        auto [ok1, lat1] = sendAndRecv(fd, buildRequest(loginMsg), resp, cfg.recv_timeout_ms);
        if (!ok1 || resp.empty()) {
            stats.recordFailure("chat_login_timeout");
            close(fd); continue;
        }
        try {
            json r = json::parse(resp);
            if (r.value("errno", -1) != 0) {
                stats.recordFailure("chat_login_business_fail");
                close(fd); continue;
            }
        } catch (...) {
            stats.recordFailure("chat_login_parse_error");
            close(fd); continue;
        }

        // 2. 发送聊天消息（ONE_CHAT_MSG 服务端无回复，只测发送吞吐）
        for (int r = 0; r < cfg.chat_repeat; r++) {
            json chatMsg;
            chatMsg["msgid"] = ONE_CHAT_MSG;
            chatMsg["id"] = userid;
            chatMsg["name"] = "bench_user_" + std::to_string(userid);
            chatMsg["toid"] = target_id;
            chatMsg["msg"] = "bench_msg_" + std::to_string(r);
            chatMsg["time"] = "2024-01-01 00:00:00";

            auto t0 = std::chrono::steady_clock::now();
            std::string framed = buildRequest(chatMsg);
            ssize_t sent = sendAll(fd, framed.data(), framed.size());
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            if (sent > 0) stats.recordSuccess(ms);
            else stats.recordFailure("chat_send_error");
        }

        close(fd);
    }
}

// 场景4：群组聊天 —— 登录后向群发消息，测试批量推送性能
void runGroupChatBatch(const WorkerConfig &cfg, BenchStats &stats, int batch_start_id, int batch_size) {
    int group_id = 1; // 预设的测试群组
    for (int i = 0; i < batch_size; i++) {
        int userid = cfg.user_base_id + (batch_start_id + i) % cfg.user_pool_size;

        int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
        if (fd < 0) {
            stats.recordFailure("connect_failed");
            continue;
        }

        // 1. 登录
        json loginMsg;
        loginMsg["msgid"] = LOGIN_MSG;
        loginMsg["id"] = userid;
        loginMsg["password"] = "bench123";

        std::string resp;
        auto [ok1, lat1] = sendAndRecv(fd, buildRequest(loginMsg), resp, cfg.recv_timeout_ms);
        if (!ok1 || resp.empty()) {
            stats.recordFailure("group_login_timeout");
            close(fd); continue;
        }
        try {
            json r = json::parse(resp);
            if (r.value("errno", -1) != 0) {
                stats.recordFailure("group_login_business_fail");
                close(fd); continue;
            }
        } catch (...) {
            stats.recordFailure("group_login_parse_error");
            close(fd); continue;
        }

        // 2. 发送群聊消息（GROUP_CHAT_MSG 服务端无回复，只测发送吞吐）
        json grpMsg;
        grpMsg["msgid"] = GROUP_CHAT_MSG;
        grpMsg["id"] = userid;
        grpMsg["name"] = "bench_user_" + std::to_string(userid);
        grpMsg["groupid"] = group_id;
        grpMsg["msg"] = "bench_group_msg";
        grpMsg["time"] = "2024-01-01 00:00:00";

        auto t0 = std::chrono::steady_clock::now();
        std::string framed = buildRequest(grpMsg);
        ssize_t sent = sendAll(fd, framed.data(), framed.size());
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (sent > 0) stats.recordSuccess(ms);
        else stats.recordFailure("group_send_error");

        close(fd);
    }
}

// 场景5：混合场景 —— 30% 注册 + 30% 登录 + 40% 聊天
void runMixedBatch(const WorkerConfig &cfg, BenchStats &stats, int batch_start_id, int batch_size) {
    for (int i = 0; i < batch_size; i++) {
        int task_id = batch_start_id + i;
        int mod = task_id % 10;

        if (mod < 3) {
            // 30% 注册
            int uid = 900000 + task_id;
            int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
            if (fd < 0) {
                stats.recordFailure("connect_failed");
                continue;
            }
            json msg;
            msg["msgid"] = REG_MSG;
            msg["name"] = "mix_user_" + std::to_string(uid);
            msg["password"] = "bench123";

            std::string resp;
            auto [ok, lat] = sendAndRecv(fd, buildRequest(msg), resp, cfg.recv_timeout_ms);
            if (ok && !resp.empty()) {
                try {
                    json r = json::parse(resp);
                    if (r.value("errno", -1) == 0) stats.recordSuccess(lat);
                    else stats.recordFailure("reg_business_fail");
                } catch (...) { stats.recordFailure("reg_parse_error"); }
            } else {
                stats.recordFailure("recv_timeout");
            }
            close(fd);
        }
        else if (mod < 6) {
            // 30% 登录（使用预加载的用户ID）
            int uid = cfg.user_base_id + (task_id % 500);
            int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
            if (fd < 0) {
                stats.recordFailure("connect_failed");
                continue;
            }
            json msg;
            msg["msgid"] = LOGIN_MSG;
            msg["id"] = uid;
            msg["password"] = "bench123";

            std::string resp;
            auto [ok, lat] = sendAndRecv(fd, buildRequest(msg), resp, cfg.recv_timeout_ms);
            if (ok && !resp.empty()) {
                try {
                    json r = json::parse(resp);
                    if (r.value("errno", -1) == 0) stats.recordSuccess(lat);
                    else stats.recordFailure("login_business_fail");
                } catch (...) { stats.recordFailure("login_parse_error"); }
            } else {
                stats.recordFailure("recv_timeout");
            }
            close(fd);
        }
        else {
            // 40% 聊天
            int uid = cfg.user_base_id + (task_id % 500);
            int fd = createConnection(cfg.ip, cfg.port, cfg.connect_timeout_ms);
            if (fd < 0) {
                stats.recordFailure("connect_failed");
                continue;
            }

            // 登录
            {
                json loginMsg;
                loginMsg["msgid"] = LOGIN_MSG;
                loginMsg["id"] = uid;
                loginMsg["password"] = "bench123";
                std::string resp;
                auto [ok2, lat2] = sendAndRecv(fd, buildRequest(loginMsg), resp, cfg.recv_timeout_ms);
                if (!ok2) { stats.recordFailure("recv_timeout"); close(fd); continue; }
            }

            // 发一条聊天消息
            json chatMsg;
            chatMsg["msgid"] = ONE_CHAT_MSG;
            chatMsg["id"] = uid;
            chatMsg["name"] = "mix_user_" + std::to_string(uid);
            chatMsg["toid"] = uid + 1;
            chatMsg["msg"] = "hello from mixed bench";
            chatMsg["time"] = "2024-01-01 00:00:00";

            std::string resp2;
            auto [ok3, lat3] = sendAndRecv(fd, buildRequest(chatMsg), resp2, cfg.recv_timeout_ms);
            if (ok3) stats.recordSuccess(lat3);
            else stats.recordFailure("recv_timeout");
            close(fd);
        }
    }
}

// ============================================================
// 线程调度器：线程池模型，每个线程领取任务执行
// ============================================================
using ScenarioFunc = std::function<void(const WorkerConfig&, BenchStats&, int, int)>;

inline ScenarioFunc getScenarioFunc(const std::string &scenario) {
    if (scenario == "register")   return runRegisterBatch;
    if (scenario == "login")      return runLoginBatch;
    if (scenario == "chat")       return runChatBatch;
    if (scenario == "groupchat")  return runGroupChatBatch;
    if (scenario == "mixed")      return runMixedBatch;
    return runLoginBatch; // 默认
}

inline void workerLoop(int thread_id, const WorkerConfig &cfg, BenchStats &stats,
                std::atomic<int> &task_counter, int total_tasks,
                ScenarioFunc scenarioFunc) {
    while (true) {
        int task_id = task_counter.fetch_add(1, std::memory_order_relaxed);
        if (task_id >= total_tasks) break;

        // 每个 task 执行 batch_size=1 的场景函数
        scenarioFunc(cfg, stats, task_id * 1, 1);
    }
}

} // namespace bench

#endif // BENCH_WORKER_HPP
