#include "redis.hpp"
#include <iostream>
using namespace std;

Redis::Redis()
    : _publish_context(nullptr), _subcribe_context(nullptr)
{
}

Redis::~Redis()
{
    if (_publish_context != nullptr)
        redisFree(_publish_context);
    if (_subcribe_context != nullptr)
        redisFree(_subcribe_context);
}

bool Redis::connect()
{
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _publish_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    _subcribe_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _subcribe_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 超短超时：observer 不长期持有 _sub_mutex
    // 10ms 内要么读到数据，要么让出锁给 subscribe/unsubscribe
    struct timeval tv = {0, 10000}; // 10ms
    redisSetTimeout(_subcribe_context, tv);

    // 使用独立线程监听订阅消息
    thread t([this]() {
        observer_channel_message();
    });
    t.detach();

    cout << "connect redis-server success!" << endl;
    return true;
}

bool Redis::publish(int channel, string message)
{
    lock_guard<mutex> lock(_pub_mutex);
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply)
    {
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::subscribe(int channel)
{
    lock_guard<mutex> lock(_sub_mutex);
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "SUBSCRIBE %d", channel))
    {
        cerr << "subscribe command failed!" << endl;
        return false;
    }
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

bool Redis::unsubscribe(int channel)
{
    lock_guard<mutex> lock(_sub_mutex);
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "UNSUBSCRIBE %d", channel))
    {
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;
    while (true)
    {
        {
            lock_guard<mutex> lock(_sub_mutex);
            if (REDIS_OK != redisGetReply(this->_subcribe_context, (void **)&reply))
            {
                // 超时是正常的，清除错误标记避免后续 subscribe/publish 失败
                // 只有连接真正断开时才退出
                if (this->_subcribe_context->err == REDIS_ERR_EOF)
                    break;
                this->_subcribe_context->err = 0;  // 清除超时等可恢复错误
                reply = nullptr;
            }
        }
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
            freeReplyObject(reply);
            reply = nullptr;
        }
        // 如果超时（reply==nullptr），短暂睡眠避免忙等
        if (reply == nullptr)
            this_thread::sleep_for(chrono::milliseconds(10));
    }
    cerr << ">>>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int, string)> fn)
{
    this->_notify_message_handler = fn;
}

// ============================================================
// 缓存方法 — Cache-Aside 模式
// 所有 value 用 %b 二进制安全传递，避免 JSON 内特殊字符截断
// ============================================================

bool Redis::cacheSet(const string& key, const string& value, int ttl)
{
    lock_guard<mutex> lock(_pub_mutex);
    redisReply *reply = (redisReply *)redisCommand(_publish_context,
        "SETEX %s %d %s",
        key.c_str(),
        ttl,
        value.c_str());
    if (reply == nullptr)
        return false;
    freeReplyObject(reply);
    return true;
}

string Redis::cacheGet(const string& key)
{
    lock_guard<mutex> lock(_pub_mutex);
    redisReply *reply = (redisReply *)redisCommand(_publish_context,
        "GET %s", key.c_str());
    if (reply == nullptr)
        return "";
    string result;
    if (reply->type == REDIS_REPLY_STRING)
        result.assign(reply->str, reply->len);
    freeReplyObject(reply);
    return result;
}

bool Redis::cacheDel(const string& key)
{
    lock_guard<mutex> lock(_pub_mutex);
    redisReply *reply = (redisReply *)redisCommand(_publish_context,
        "DEL %s", key.c_str());
    if (reply == nullptr)
    {
        cerr << "Redis::cacheDel command failed" << endl;
        return false;
    }
    // DEL 返回删除的 key 数量
    bool ok = (reply->type == REDIS_REPLY_INTEGER);
    freeReplyObject(reply);
    return ok;
}
