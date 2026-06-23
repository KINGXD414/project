
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include "json.hpp"
#include <atomic>
//测试3：
//#if 0
using json = nlohmann::json;
using namespace std;
using namespace chrono;

vector<double> g_latencies;
mutex g_mtx;
int g_success = 0;
int g_fail = 0;

void worker(int test_type)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        lock_guard<mutex> lock(g_mtx);
        g_fail++;
        close(sock);
        return;
    }

    auto start = high_resolution_clock::now();

    json msg;
    
    if (test_type == 1) {
        // 注册（轻量操作）
        static atomic<int> id_counter{10000};
        int uid = id_counter++;
        msg["msgid"] = 4; // REG_MSG
        msg["name"] = "test_user_" + to_string(uid);
        msg["password"] = "123456";
    }
    else if (test_type == 2) {
        // 登录（中等操作，查DB+更新）
        msg["msgid"] = 1; // LOGIN_MSG
        msg["id"] = 1001; // 用已存在的用户ID
        msg["password"] = "123456";
    }

    string data = msg.dump() + "\r\n";
    send(sock, data.c_str(), data.size(), 0);

    char buf[4096] = {0};
    int n = recv(sock, buf, sizeof(buf)-1, 0);

    auto end = high_resolution_clock::now();
    double ms = duration<double, milli>(end - start).count();

    if (n > 0) {
        lock_guard<mutex> lock(g_mtx);
        g_latencies.push_back(ms);
        g_success++;
    } else {
        lock_guard<mutex> lock(g_mtx);
        g_fail++;
    }

    close(sock);
}

void run_test(int concurrency, int total, int test_type, const string& label)
{
    g_latencies.clear();
    g_success = 0;
    g_fail = 0;

    cout << "\n========== " << label << " ==========" << endl;
    cout << "并发: " << concurrency << ", 总请求: " << total << endl;

    auto total_start = high_resolution_clock::now();

    vector<thread> threads;
    for (int i = 0; i < total; i++)
    {
        if (threads.size() >= concurrency)
        {
            threads[i % concurrency].join();
            threads[i % concurrency] = thread(worker, test_type);
        }
        else
        {
            threads.emplace_back(worker, test_type);
        }
    }

    for (auto &t : threads) t.join();

    auto total_end = high_resolution_clock::now();
    double total_ms = duration<double, milli>(total_end - total_start).count();

    sort(g_latencies.begin(), g_latencies.end());
    double sum = 0;
    for (auto x : g_latencies) sum += x;
    double avg = sum / g_latencies.size();
    double p50 = g_latencies[g_latencies.size() * 0.5];
    double p95 = g_latencies[g_latencies.size() * 0.95];
    double p99 = g_latencies[g_latencies.size() * 0.99];

    cout << "QPS: " << (g_success / (total_ms / 1000)) << endl;
    cout << "成功: " << g_success << ", 失败: " << g_fail << endl;
    cout << "平均延迟: " << avg << " ms" << endl;
    cout << "P50: " << p50 << " ms" << endl;
    cout << "P95: " << p95 << " ms" << endl;
    cout << "P99: " << p99 << " ms" << endl;
}

int main()
{
    int concurrency[] = {50, 100, 200, 500};
    int total = 2000; // 每个测试跑2000请求

    cout << "=== 先启动 ChatServer，然后按回车开始 ===" << endl;
    getchar();

    for (int c : concurrency) {
        // 测试1：注册
        run_test(c, total, 1, "注册操作 - 并发" + to_string(c));
        
        // 测试2：登录
        run_test(c, total, 2, "登录操作 - 并发" + to_string(c));
        
        this_thread::sleep_for(seconds(2));
    }

    return 0;
}
//#endif

//测试2：高并发
#if 0

using json = nlohmann::json;
using namespace std;
using namespace chrono;

vector<double> g_latencies;
mutex g_mtx;
int g_success = 0;
int g_fail = 0;

void worker(int id) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6000);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        lock_guard<mutex> lock(g_mtx);
        g_fail++;
        close(sock);
        return;
    }

    auto start = high_resolution_clock::now();

    json msg;
    msg["msgid"] = 4;  // REG_MSG = 4
    msg["name"] = "bench_user_" + to_string(10000 + id);
    msg["password"] = "123456";
    string data = msg.dump() + "\r\n";
    send(sock, data.c_str(), data.size(), 0);

    char buf[4096] = {0};
    int n = recv(sock, buf, sizeof(buf) - 1, 0);

    auto end = high_resolution_clock::now();
    double ms = duration<double, milli>(end - start).count();

    lock_guard<mutex> lock(g_mtx);
    if (n > 0) {
        g_latencies.push_back(ms);
        g_success++;
    } else {
        g_fail++;
    }
    close(sock);
}

int main() {
    int concurrency = 100;
    int total = 1000;

    cout << "=== 启动压测 ===" << endl;
    cout << "并发: " << concurrency << ", 总请求: " << total << endl;

    auto total_start = high_resolution_clock::now();

    // 用批次并发：每批 concurrency 个，跑完再开下一批
    int done = 0;
    while (done < total) {
        int batch = min(concurrency, total - done);
        vector<thread> threads;
        for (int i = 0; i < batch; i++) {
            threads.emplace_back(worker, done + i);
        }
        for (auto &t : threads) t.join();
        done += batch;
    }

    auto total_end = high_resolution_clock::now();
    double total_ms = duration<double, milli>(total_end - total_start).count();

    sort(g_latencies.begin(), g_latencies.end());
    double sum = 0;
    for (auto x : g_latencies) sum += x;
    double avg = g_latencies.empty() ? 0 : sum / g_latencies.size();
    double p50 = g_latencies[g_latencies.size() * 0.5];
    double p95 = g_latencies[g_latencies.size() * 0.95];
    double p99 = g_latencies[g_latencies.size() * 0.99];

    cout << "\n========== 压测结果 ==========\n";
    cout << "QPS: " << (g_success / (total_ms / 1000)) << "\n";
    cout << "成功: " << g_success << ", 失败: " << g_fail << "\n";
    cout << "平均延迟: " << avg << " ms\n";
    cout << "P50: " << p50 << " ms\n";
    cout << "P95: " << p95 << " ms\n";
    cout << "P99: " << p99 << " ms\n";
    cout << "==============================\n";
    return 0;
}
#endif

//测试1：单个测试
#if 0
using json = nlohmann::json;

int main() {
    const char* ip = "127.0.0.1";
    int port = 6000;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "socket error" << std::endl;
        return 1;
    }

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "connect error" << std::endl;
        close(sock);
        return 1;
    }

    // 构造和服务器完全兼容的注册消息
    json msg;
    msg["msgid"] = 4; // REG_MSG，对应你项目的注册消息ID
    msg["name"] = "bench_test_1";
    msg["password"] = "123456";

    // 必须是 \r\n，不能错
    std::string data = msg.dump() + "\r\n";

    // 发送数据
    send(sock, data.c_str(), data.size(), 0);
    std::cout << "Sent: " << data << std::endl;

    // 等待服务器响应
    char buf[4096] = {0};
    int ret = recv(sock, buf, sizeof(buf)-1, 0);
    if (ret > 0) {
        std::cout << "Recv: " << buf << std::endl;
    } else {
        std::cerr << "recv error" << std::endl;
    }

    close(sock);
    return 0;
}
#endif