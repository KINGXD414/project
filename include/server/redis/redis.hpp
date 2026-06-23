#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <mutex>
using namespace std;

class Redis
{
public:
    Redis();
    ~Redis();

    bool connect();
    bool publish(int channel, string message);
    bool subscribe(int channel);
    bool unsubscribe(int channel);
    void observer_channel_message();
    void init_notify_handler(function<void(int, string)> fn);

    // ====== 缓存方法 ======
    // SETEX key ttl value（二进制安全，用%b）
    bool cacheSet(const string& key, const string& value, int ttl = 300);
    // GET key，不存在返回空字符串
    string cacheGet(const string& key);
    // DEL key
    bool cacheDel(const string& key);

private:
    redisContext *_publish_context;
    redisContext *_subcribe_context;
    function<void(int, string)> _notify_message_handler;
    mutex _pub_mutex;   // 保护 publish_context
    mutex _sub_mutex;    // 保护 subscribe/unsubscribe 写操作
};

#endif
