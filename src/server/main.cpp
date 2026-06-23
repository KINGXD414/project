#include "chatserver.hpp"
#include "chatservice.hpp"
#include "FixedThreadPool.hpp"
#include "connectionpool.hpp"
#include <iostream>
#include <signal.h>
#include <string.h>
using namespace std;

pool::FixedThreadPool *g_pool = nullptr;

void resetHandler(int)
{
    ChatService::instance()->reset();
    if (g_pool) g_pool->Stop();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000 [--no-pool]" << endl;
        exit(-1);
    }

    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 解析可选参数：--no-pool 禁用业务线程池，业务直接在 IO 线程执行
    bool usePool = true;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--no-pool") == 0) {
            usePool = false;
        }
    }

    // 初始化 MySQL 连接池（根据是否用线程池调整大小）
    int poolSize = usePool ? 32 : 4;
    ConnectionPool::instance()->init("127.0.0.1", "root", "123456", "chat", 3306, poolSize);

    if (usePool)
    {
        pool::FixedThreadPool businessPool(5000, thread::hardware_concurrency());
        g_pool = &businessPool;
        ChatService::instance()->setThreadPool(&businessPool);
        cout << ">>> Mode: WITH ThreadPool (size=" << thread::hardware_concurrency() << "), DB pool=" << poolSize << endl;

        signal(SIGINT, resetHandler);

        EventLoop loop;
        InetAddress addr(ip, port);
        ChatServer server(&loop, addr, "ChatServer");
        server.start();
        loop.loop();
    }
    else
    {
        cout << ">>> Mode: NO ThreadPool (IO threads only), DB pool=" << poolSize << endl;

        signal(SIGINT, resetHandler);

        EventLoop loop;
        InetAddress addr(ip, port);
        ChatServer server(&loop, addr, "ChatServer");
        server.start();
        loop.loop();
    }

    return 0;
}
