#ifndef BENCH_STATS_HPP
#define BENCH_STATS_HPP

#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <string>
#include <string.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <unordered_map>

namespace bench {

struct BenchStats {
    // 原子计数器：请求总数与成功/失败
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> success_count{0};
    std::atomic<uint64_t> fail_count{0};

    // 按错误类型分类
    std::mutex error_mtx;
    std::unordered_map<std::string, std::atomic<uint64_t>> error_map;

    // 延迟数据（单位：毫秒）
    std::mutex latency_mtx;
    std::vector<double> latencies;

    // 测试开始时间
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;

    void markStart() {
        start_time = std::chrono::steady_clock::now();
    }

    void markEnd() {
        end_time = std::chrono::steady_clock::now();
    }

    void recordSuccess(double latency_ms) {
        success_count.fetch_add(1, std::memory_order_relaxed);
        total_requests.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(latency_mtx);
        latencies.push_back(latency_ms);
    }

    void recordFailure(const std::string &error_type) {
        fail_count.fetch_add(1, std::memory_order_relaxed);
        total_requests.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(error_mtx);
        auto it = error_map.find(error_type);
        if (it == error_map.end()) {
            error_map[error_type].store(1);
        } else {
            it->second.fetch_add(1);
        }
    }

    double durationSeconds() const {
        return std::chrono::duration<double>(end_time - start_time).count();
    }

    double qps() const {
        double dur = durationSeconds();
        return dur > 0 ? success_count.load() / dur : 0;
    }

    double percentile(double p) const {
        if (latencies.empty()) return 0;
        std::vector<double> sorted(latencies);
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * p);
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    double avgLatency() const {
        if (latencies.empty()) return 0;
        double sum = 0;
        for (double v : latencies) sum += v;
        return sum / latencies.size();
    }

    double minLatency() const {
        if (latencies.empty()) return 0;
        return *std::min_element(latencies.begin(), latencies.end());
    }

    double maxLatency() const {
        if (latencies.empty()) return 0;
        return *std::max_element(latencies.begin(), latencies.end());
    }

    void printReport(const std::string &scenario_name, int concurrency) const {
        uint64_t total = total_requests.load();
        uint64_t succ = success_count.load();
        uint64_t fail = fail_count.load();
        double dur = durationSeconds();

        std::cout << std::endl;
        std::cout << "╔══════════════════════════════════════════╗" << std::endl;
        std::cout << "║          BENCHMARK REPORT               ║" << std::endl;
        std::cout << "╠══════════════════════════════════════════╣" << std::endl;
        std::cout << "║ Scenario:       " << std::left << std::setw(25) << scenario_name << "║" << std::endl;
        std::cout << "║ Concurrency:    " << std::left << std::setw(25) << concurrency << "║" << std::endl;
        std::cout << "║ Total requests: " << std::left << std::setw(25) << total << "║" << std::endl;
        std::cout << "║ Duration:       " << std::left << std::setw(20) << (std::to_string(dur) + " s") << " ║" << std::endl;
        std::cout << "╠══════════════════════════════════════════╣" << std::endl;
        std::cout << "║ QPS:            " << std::left << std::setw(24) << qps() << " ║" << std::endl;
        std::cout << "║ Success:        " << std::left << std::setw(20) << (std::to_string(succ) + " (" + std::to_string(total > 0 ? succ * 100.0 / total : 0) + "%)") << " ║" << std::endl;
        std::cout << "║ Failures:       " << std::left << std::setw(20) << (std::to_string(fail) + " (" + std::to_string(total > 0 ? fail * 100.0 / total : 0) + "%)") << " ║" << std::endl;
        std::cout << "╠══════════════════════════════════════════╣" << std::endl;
        std::cout << "║ Latency (ms):                           ║" << std::endl;
        std::cout << "║   Avg:   " << std::left << std::setw(28) << avgLatency() << " ║" << std::endl;
        std::cout << "║   Min:   " << std::left << std::setw(28) << minLatency() << " ║" << std::endl;
        std::cout << "║   Max:   " << std::left << std::setw(28) << maxLatency() << " ║" << std::endl;
        std::cout << "║   P50:   " << std::left << std::setw(28) << percentile(0.50) << " ║" << std::endl;
        std::cout << "║   P95:   " << std::left << std::setw(28) << percentile(0.95) << " ║" << std::endl;
        std::cout << "║   P99:   " << std::left << std::setw(28) << percentile(0.99) << " ║" << std::endl;
        std::cout << "║   P999:  " << std::left << std::setw(28) << percentile(0.999) << " ║" << std::endl;

        if (!error_map.empty()) {
            std::cout << "╠══════════════════════════════════════════╣" << std::endl;
            std::cout << "║ Error distribution:                     ║" << std::endl;
            for (auto &kv : error_map) {
                std::cout << "║   " << std::left << std::setw(36) << (kv.first + ": " + std::to_string(kv.second.load())) << "║" << std::endl;
            }
        }
        std::cout << "╚══════════════════════════════════════════╝" << std::endl;
    }
};

} // namespace bench

#endif // BENCH_STATS_HPP
