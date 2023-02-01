#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>

class EventLoop;

/**
 * EventLoop(Channel + Poller) 对应 reactor 模型中的 Demultiplex
 * Channel（通道），封装了sockfd和其感兴趣的event，如EPOLLIN，EPOLLOUT
 * 还绑定了poller返回的具体事件
 */
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop *loop, int fd);
    ~Channel();

    // fd得到poller通知以后，用来处理事件的
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止Channel被手动remove掉，之后还在执行回调操作
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return event_; }
    int set_revent(int revt) { revent_ = revt; }

    // 设置fd相应的事件状态
    void enableReading() { event_ |= kReadEvent; update(); }
    void disableReading() { event_ &= ~kReadEvent; update(); }
    void enableWriting() { event_ |= kWriteEvent; update(); }
    void disableWriting() { event_ &= ~kWriteEvent; update(); }
    void disableAll() { event_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return event_ == kNoneEvent; }
    bool isReading() const { return event_ & kReadEvent; }
    bool isWriting() const { return event_ & kWriteEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:

    void update();
    void handleEventWithGuard(Timestamp receiveTime);

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_; ///< 事件循环
    const int fd_;    ///< poller监听的对象
    int event_;       ///< 注册fd感兴趣的事件
    int revent_;      ///< poller返回具体发生的事件
    int index_;

    std::weak_ptr<void> tie_;
    bool tied_;

    // 因为Channel里面能够感知fd最终发生的具体时间revents，所以负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};