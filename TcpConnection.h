#pragma once

#include "noncopyable.h"
#include "InetAddress.h"
#include "Callbacks.h"
#include "Buffer.h"
#include "Timestamp.h"

#include <memory>
#include <string>
#include <atomic>

class Channel;
class EventLoop;
class Socket;

/**
 * TcpServer => Acceptor => 有一个新用户连接，通过accept得到connfd
 * => 打包成TcpConnection，设置回调 => Channel => Poller => Channel的回调操作
 */
class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop *loop,
                  const std::string nameArg,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; }
    const std::string &getName() const { return name_; }
    const InetAddress &localAddress() const { return localAddr_; }
    const InetAddress &peerAddress() const { return peerAddr_; }

    bool connected() const { return state_ == kConnected; }
    bool disconnected() const { return state_ == kDisConnected; }

    // 发送数据
    void send(const std::string &buf);
    // 关闭连接
    void shutdown();

    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb) { highWaterMarkCallback_ = cb; }
    void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }

    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

private:
    enum StateE
    {
        kDisConnected,
        kConnecting,
        kConnected,
        kDisConnecting
    };

    void setState(StateE state) { state_ = state; };

    void handleRead(Timestamp receiveTime);
    void handleWrite();
    void handleError();
    void handleClose();

    void sendInLoop(const void *data, size_t len);

    // void shutdown();
    void shutdownInLoop();

    EventLoop *loop_; ///< 这里肯定不是mainLoop，因为TcpConnection都是在subLoop中管理的
    const std::string name_;
    std::atomic_int state_;
    bool reading_;

    // 这里和Acceptor类似：
    //         acceptor是工作在mainloop中      TcpConnection工作在subloop中 
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    ConnectionCallback connectionCallback_;       ///< 有新连接时的回调
    MessageCallback messageCallback_;             ///< 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; ///< 消息发送完以后的回调
    HighWaterMarkCallback highWaterMarkCallback_;
    CloseCallback closeCallback_;

    size_t highWaterMark_;

    Buffer inputBuffer_;    ///< 接受数据的缓冲区
    Buffer outputBuffer_;   ///< 发送数据的缓冲区
};