#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

// ============================================================
// 长度前缀帧协议 (Length-Prefixed Framing)
//
// 帧格式:
//   +-----------------+---------------------------+
//   |  4字节 Length    |        Payload            |
//   |  uint32_t (大端) |   (JSON消息体)             |
//   +-----------------+---------------------------+
//
// 时间复杂度: O(1) — 读4字节即知消息边界，无需扫描缓冲区
// ============================================================

#include <string>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// 单条消息最大64KB，防止恶意超大长度攻击
static constexpr uint32_t kMaxMessageSize = 64 * 1024;

// ============================================================
// Raw Socket 基础 IO（处理部分读/写）
// ============================================================

// 从 socket 精确读取 N 字节（循环 recv 直到读满）
inline ssize_t recvAll(int fd, void* buf, size_t len)
{
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);
    while (total < len)
    {
        ssize_t n = recv(fd, ptr + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return static_cast<ssize_t>(total);
}

// 向 socket 精确写入 N 字节（循环 send 直到写完）
inline ssize_t sendAll(int fd, const void* data, size_t len)
{
    size_t total = 0;
    const char* ptr = static_cast<const char*>(data);
    while (total < len)
    {
        ssize_t n = send(fd, ptr + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return static_cast<ssize_t>(total);
}

// ============================================================
// 帧协议读写（Raw Socket 版本）
// ============================================================

// 发送：4字节长度(大端) + payload
// 返回: 成功发送的总字节数(含4字节头), 失败返回-1
inline ssize_t sendWithLength(int fd, const std::string& payload)
{
    uint32_t netLen = htonl(static_cast<uint32_t>(payload.size()));
    if (sendAll(fd, &netLen, 4) < 0) return -1;
    return sendAll(fd, payload.data(), payload.size());
}

// 接收：读4字节长度 → 读len字节body
// 返回: true=成功(out中为消息体), false=连接断开/长度非法
inline bool recvWithLength(int fd, std::string& out)
{
    uint32_t netLen = 0;
    if (recvAll(fd, &netLen, 4) <= 0) return false;

    uint32_t len = ntohl(netLen);
    if (len > kMaxMessageSize) return false;

    out.resize(len);
    return recvAll(fd, &out[0], len) > 0;
}

#endif // PROTOCOL_HPP
