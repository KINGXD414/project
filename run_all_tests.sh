#!/bin/bash
# 完整压测：维度2(全API)、维度3(中间件)、维度4(浸泡)
BIN=/home/xd/code/MuduoChatServer/bin
LOG=/tmp/srv_final.log

run_one() {
    local label="$1"; local extra="$2"; local conc="$3"; local total="$4"; local scenario="$5"
    printf "\n[%s] conc=%s total=%s scenario=%s\n" "$label" "$conc" "$total" "$scenario"
    fuser -k 6000/tcp 2>/dev/null; sleep 1
    # 重置状态
    mysql -u root -p123456 chat -e "UPDATE user SET state=0 WHERE state=1;" 2>/dev/null
    $BIN/ChatServer 127.0.0.1 6000 $extra > $LOG 2>&1 & sleep 3
    $BIN/ChatBench --ip 127.0.0.1 --port 6000 --scenario "$scenario" --concurrency $conc --total $total 2>/dev/null \
        | grep -E "QPS:|Success:|Failures:|P50:|P95:|P99:|connect|login_business|login_recv|chat_"
    pgrep ChatServer > /dev/null && echo "  [SERVER OK]" || echo "  [SERVER CRASHED!]"
}

echo "╔══════════════════════════════════════╗"
echo "║   FULL BENCHMARK SUITE             ║"
echo "╚══════════════════════════════════════╝"

# ===== 维度2: 登录QPS梯度 =====
echo ""; echo "=== D2a: Login QPS Gradient ==="
for c in 10 30 50 100; do
    run_one "LOGIN conc=$c" "" $c 500 login
done

# ===== 维度2: 注册QPS =====
echo ""; echo "=== D2b: Register QPS ==="
run_one "REGISTER conc=100" "" 100 500 register

# ===== 维度2: 聊天单聊 =====
echo ""; echo "=== D2c: One-to-One Chat ==="
run_one "CHAT conc=30" "" 30 100 chat

# ===== 维度3: 无线程池对比 =====
echo ""; echo "=== D3: No ThreadPool (IO only) ==="
run_one "NO-POOL Login conc=50" "--no-pool" 50 500 login

# ===== 维度4: 浸泡测试 (30并发, 10轮) =====
echo ""; echo "=== D4: Soak Test (30 conc, 10 rounds) ==="
fuser -k 6000/tcp 2>/dev/null; sleep 1
mysql -u root -p123456 chat -e "UPDATE user SET state=0 WHERE state=1;" 2>/dev/null
$BIN/ChatServer 127.0.0.1 6000 > $LOG 2>&1 &
SERVER_PID=$!
sleep 3
echo "Soak start: PID=$SERVER_PID"
INITIAL_RSS=$(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null)
INITIAL_FD=$(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)
echo "Initial RSS=${INITIAL_RSS}KB FDs=$INITIAL_FD"

for r in $(seq 1 10); do
    mysql -u root -p123456 chat -e "UPDATE user SET state=0 WHERE state=1;" 2>/dev/null
    $BIN/ChatBench --ip 127.0.0.1 --port 6000 --scenario login --concurrency 30 --total 300 2>/dev/null \
        | grep -E "QPS:|Success:|P50:|P95:" | while read line; do echo "  Round $r: $line"; done
    CURRENT_RSS=$(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null)
    CURRENT_FD=$(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)
    echo "  Round $r stats: RSS=${CURRENT_RSS}KB FDs=$CURRENT_FD"
done

FINAL_RSS=$(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null)
FINAL_FD=$(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)
echo "Final RSS=${FINAL_RSS}KB FDs=$FINAL_FD"
echo "RSS delta: $((FINAL_RSS - INITIAL_RSS))KB, FD delta: $((FINAL_FD - INITIAL_FD))"
fuser -k 6000/tcp 2>/dev/null

echo ""; echo "=== ALL TESTS COMPLETE ==="
