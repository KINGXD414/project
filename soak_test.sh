#!/bin/bash
# 维度4: 稳定性浸泡测试 — 80% 峰值压力持续运行，监控内存/CPU/连接泄露
BIN=/home/xd/code/MuduoChatServer/bin
LOG=/tmp/soak_server.log
STATFILE=/tmp/soak_stats.txt

DURATION=300  # 5分钟
CONC=80       # 80% of peak 100
QPS_TARGET=200

echo "╔══════════════════════════════════════════════╗"
echo "║  DIMENSION 4: Stability Soak Test           ║"
echo "║  Duration: ${DURATION}s (5 min)              ║"
echo "║  Concurrency: $CONC (~80% peak)              ║"
echo "╚══════════════════════════════════════════════╝"

# 重置DB状态
mysql -u root -p123456 chat -e "UPDATE user SET state=0 WHERE state=1;" 2>/dev/null

fuser -k 6000/tcp 2>/dev/null
sleep 1

# 启动服务器
$BIN/ChatServer 127.0.0.1 6000 > $LOG 2>&1 &
SERVER_PID=$!
sleep 2
echo "Server PID: $SERVER_PID"

# 记录初始状态
echo "=== INITIAL STATE ==="
echo "Server PID: $SERVER_PID"
echo "RSS (KB): $(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null || echo N/A)"
echo "FD count: $(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)"
echo "DB connections: $(mysql -u root -p123456 -e "SHOW PROCESSLIST;" 2>/dev/null | wc -l)"

# 开始监控+压测
echo ""
echo "=== SOAK TEST $(date) ==="
echo "Timestamp,Elapsed(s),RSS_KB,FD_Count,DB_Conn,CPU_Pct" > $STATFILE

# 后台监控线程
(
    START_TS=$(date +%s)
    while true; do
        NOW_TS=$(date +%s)
        ELAPSED=$((NOW_TS - START_TS))
        if [ $ELAPSED -gt $DURATION ]; then break; fi
        RSS=$(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null || echo 0)
        FD=$(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)
        DB=$(mysql -u root -p123456 -e "SHOW PROCESSLIST;" 2>/dev/null | grep -c Sleep)
        CPU=$(ps -p $SERVER_PID -o %cpu --no-headers 2>/dev/null | tr -d ' ')
        echo "$(date +%H:%M:%S),$ELAPSED,$RSS,$FD,$DB,$CPU" >> $STATFILE
        sleep 10
    done
) &
MONITOR_PID=$!

# 多轮压测
ROUND=0
while true; do
    START_TS=$(date +%s)
    ROUND=$((ROUND + 1))
    echo "[Round $ROUND] Starting at $(date +%H:%M:%S)..."

    $BIN/ChatBench --ip 127.0.0.1 --port 6000 --scenario login \
        --concurrency $CONC --total 500 2>/dev/null

    NOW_TS=$(date +%s)
    ELAPSED=$((NOW_TS - START_TS))
    if [ $ELAPSED -gt $DURATION ]; then break; fi
done

echo ""
echo "=== Soak test complete. Killing server ==="
kill $MONITOR_PID 2>/dev/null

# 最终状态
echo ""
echo "=== FINAL STATE ==="
RSS=$(awk '/VmRSS/{print $2}' /proc/$SERVER_PID/status 2>/dev/null || echo 0)
FD=$(ls /proc/$SERVER_PID/fd 2>/dev/null | wc -l)
echo "RSS (KB): $RSS"
echo "FD count: $FD"

fuser -k 6000/tcp 2>/dev/null

# 输出统计摘要
echo ""
echo "=== STABILITY SUMMARY ==="
echo "Timeline saved to $STATFILE"
awk -F',' 'NR>1 {
    rss+=$3; fd+=$4; db+=$5; cpu+=$6; n++
} END {
    if(n>0) printf("Avg RSS: %.0f KB\nAvg FDs: %.0f\nAvg DB Conn: %.0f\nAvg CPU: %.1f%%\nSamples: %d\n", rss/n, fd/n, db/n, cpu/n, n)
}' $STATFILE

# 检测内存泄漏趋势
echo ""
echo "=== LEAK DETECTION ==="
awk -F',' 'NR>1 {
    if(NR==2){first_rss=$3; first_fd=$4}
    last_rss=$3; last_fd=$4
} END {
    printf("RSS change: %.0f KB -> %.0f KB (%+.1f%%)\n", first_rss, last_rss, (last_rss-first_rss)/first_rss*100)
    printf("FD  change: %.0f -> %.0f (%+.0f)\n", first_fd, last_fd, last_fd-first_fd)
}' $STATFILE

echo "=== DONE ==="
