/**
 * ChatServer 压测工具
 *
 * 用法:
 *   ./ChatBench --ip 127.0.0.1 --port 6000 --scenario login --concurrency 100 --total 10000
 *   ./ChatBench --ip 127.0.0.1 --port 6000 --scenario chat   --concurrency  50 --total  1000 --repeat 5
 *   ./ChatBench --scenario register --total 5000
 *
 * 场景 (--scenario):
 *   register  - 注册新用户
 *   login     - 登录已有用户
 *   chat      - 登录后一对一聊天
 *   mixed     - 混合场景
 */

#include "json.hpp"
#include "public.hpp"
#include "bench_stats.hpp"
#include "bench_worker.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <string.h>
#include <getopt.h>

using json = nlohmann::json;
using namespace bench;

void printUsage(const char *prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "Options:\n"
              << "  --ip <ip>           Server IP (default: 127.0.0.1)\n"
              << "  --port <port>       Server port (default: 6000)\n"
              << "  --scenario <name>   Test scenario: register | login | chat | groupchat | mixed (default: login)\n"
              << "  --concurrency <n>   Number of concurrent threads (default: 10)\n"
              << "  --total <n>         Total number of requests (default: 1000)\n"
              << "  --repeat <n>        Chat messages per connection, for 'chat' scenario (default: 3)\n"
              << "  --warmup <n>        Number of warmup requests before test (default: 0)\n"
              << "  --help              Show this help\n"
              << std::endl;
}

int main(int argc, char *argv[]) {
    WorkerConfig cfg;

    // ========== 解析命令行参数 ==========
    struct option long_opts[] = {
        {"ip",          required_argument, 0, 1},
        {"port",        required_argument, 0, 2},
        {"scenario",    required_argument, 0, 3},
        {"concurrency", required_argument, 0, 4},
        {"total",       required_argument, 0, 5},
        {"repeat",      required_argument, 0, 6},
        {"warmup",      required_argument, 0, 7},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int concurrency = 10;
    int total_requests = 1000;
    int warmup = 0;

    while ((opt = getopt_long_only(argc, argv, "", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 1: cfg.ip = optarg; break;
            case 2: cfg.port = atoi(optarg); break;
            case 3: cfg.scenario = optarg; break;
            case 4: concurrency = atoi(optarg); break;
            case 5: total_requests = atoi(optarg); break;
            case 6: cfg.chat_repeat = atoi(optarg); break;
            case 7: warmup = atoi(optarg); break;
            case 'h':
            default:
                printUsage(argv[0]);
                return opt == 'h' ? 0 : 1;
        }
    }

    cfg.total_requests = total_requests;

    // 验证参数
    if (concurrency <= 0 || total_requests <= 0) {
        std::cerr << "Error: concurrency and total must be > 0" << std::endl;
        return 1;
    }

    // ========== 打印配置 ==========
    std::cout << "╔══════════════════════════════════════════╗" << std::endl;
    std::cout << "║        ChatServer Benchmark Tool         ║" << std::endl;
    std::cout << "╠══════════════════════════════════════════╣" << std::endl;
    std::cout << "║ Target:   " << cfg.ip << ":" << cfg.port << std::string(26 - cfg.ip.length(), ' ') << "║" << std::endl;
    std::cout << "║ Scenario: " << std::left << std::setw(28) << cfg.scenario << "║" << std::endl;
    std::cout << "║ Threads:  " << std::left << std::setw(28) << concurrency << "║" << std::endl;
    std::cout << "║ Total:    " << std::left << std::setw(28) << total_requests << "║" << std::endl;
    if (cfg.chat_repeat > 1)
    std::cout << "║ Repeat:   " << std::left << std::setw(28) << cfg.chat_repeat << "║" << std::endl;
    std::cout << "╚══════════════════════════════════════════╝" << std::endl;

    // ========== 选择场景函数 ==========
    auto scenarioFunc = getScenarioFunc(cfg.scenario);
    BenchStats stats;

    // ========== Warmup（可选） ==========
    if (warmup > 0) {
        std::cout << "\n[WARMUP] Running " << warmup << " warmup requests..." << std::flush;
        BenchStats warmupStats;
        warmupStats.markStart();
        std::atomic<int> warmupCounter{0};
        int warmupThreads = std::min(concurrency, warmup);
        std::vector<std::thread> warmupWorkers;
        for (int i = 0; i < warmupThreads; i++) {
            warmupWorkers.emplace_back([&, i]() {
                int batchBegin = i * (warmup / warmupThreads);
                int batchSize = (i == warmupThreads - 1)
                    ? warmup - batchBegin
                    : warmup / warmupThreads;
                scenarioFunc(cfg, warmupStats, batchBegin, batchSize);
            });
        }
        for (auto &t : warmupWorkers) t.join();
        warmupStats.markEnd();
        std::cout << " done (" << warmupStats.success_count.load() << " ok, "
                  << warmupStats.fail_count.load() << " fail)" << std::endl;
    }

    // ========== 正式测试 ==========
    std::cout << "\n[RUN] Starting benchmark..." << std::endl;

    stats.markStart();

    std::atomic<int> taskCounter{0};
    std::vector<std::thread> workers;

    // 每个线程平等分配任务
    for (int i = 0; i < concurrency; i++) {
        int batch_begin = i * (total_requests / concurrency);
        int batch_size = (i == concurrency - 1)
            ? total_requests - batch_begin
            : total_requests / concurrency;

        workers.emplace_back([&, batch_begin, batch_size]() {
            for (int j = 0; j < batch_size; j++) {
                int task_id = batch_begin + j;
                scenarioFunc(cfg, stats, task_id, 1);
            }
        });
    }

    // 实时进度打印
    int prev_report = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t done = stats.total_requests.load();
        int pct = (int)(done * 100 / total_requests);
        if (pct >= 100) break;
        if (pct > prev_report) {
            std::cout << "  Progress: " << pct << "% (" << done << "/" << total_requests << ")"
                      << "  QPS: " << (int)stats.qps()
                      << "  Fail: " << stats.fail_count.load() << "\r" << std::flush;
            prev_report = pct;
        }
    }

    for (auto &t : workers) t.join();

    stats.markEnd();

    // ========== 输出报告 ==========
    stats.printReport(cfg.scenario, concurrency);

    return 0;
}
