/**
 * 并发连接数压测 — 只建连不收发，测试最大 TCP 长连接上限
 * 用法: ./ConnBench --ip 127.0.0.1 --port 6000 --connections 10000 --step 1000
 */
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[]) {
    std::string ip = "127.0.0.1";
    int port = 6000;
    int max_conn = 10000;
    int step = 1000;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ip" && i+1 < argc) ip = argv[++i];
        else if (arg == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--connections" && i+1 < argc) max_conn = std::atoi(argv[++i]);
        else if (arg == "--step" && i+1 < argc) step = std::atoi(argv[++i]);
    }

    std::cout << "=== TCP Connection Benchmark ===" << std::endl;
    std::cout << "Target: " << ip << ":" << port << std::endl;
    std::cout << "Max connections: " << max_conn << " (step=" << step << ")" << std::endl;
    std::cout << "=================================" << std::endl;

    std::vector<int> fds;
    fds.reserve(max_conn);

    auto start = std::chrono::steady_clock::now();
    int success_count = 0;
    int fail_count = 0;

    for (int i = 0; i < max_conn; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            fail_count++;
            break;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            fail_count++;
            close(fd);
            break;
        }

        // 设置 keep-alive
        int keepalive = 1;
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

        fds.push_back(fd);
        success_count++;

        // 每 step 个连接报告一次
        if ((i + 1) % step == 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            std::cout << "  [" << (i + 1) << "/" << max_conn << "] connected, "
                      << "elapsed=" << elapsed << "s, "
                      << "conn/s=" << (int)((i + 1) / elapsed) << std::endl;
        }
    }

    auto end = std::chrono::steady_clock::now();
    double total_time = std::chrono::duration<double>(end - start).count();

    std::cout << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << "  CONNECTION BENCH RESULT" << std::endl;
    std::cout << "  Max established: " << success_count << std::endl;
    std::cout << "  Failed:          " << fail_count << std::endl;
    std::cout << "  Total time:      " << total_time << " s" << std::endl;
    std::cout << "  Conn/s:          " << (int)(success_count / total_time) << std::endl;
    std::cout << "=========================================" << std::endl;

    // 保持连接 30 秒观察服务器内存
    if (success_count > 0) {
        std::cout << "\n[HOLD] Keeping " << success_count << " connections alive for 30s..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    // 关闭所有连接
    for (int fd : fds) close(fd);
    std::cout << "[DONE] All connections closed." << std::endl;

    return 0;
}
