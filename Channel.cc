#include "Channel.h"
#include "EventLoop.h"
#include "Logger.h"

#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , event_(0)
    , revent_(0)
    , index_(-1)
    , tied_(false)
{
}

Channel::~Channel()
{
}

// Channel的tie方法什么时候调用过？一个TcpConnection新创建连接的时候 TcpConnection => Channel
void Channel::tie(const std::shared_ptr<void> &obj)
{
    tie_ = obj;
    tied_ = true;
}

/**
 * 当改变channel所表示fd的event事件后，update负责在poller里更改fd相应的事件（epoll_ctl)
 * EventLoop => ChannelList + Poller
 */
void Channel::update()
{
    // 通过channel所属的EventLoop，调用Poller的相关方法，注册fd的相关事件
    loop_->updateChannel(this);
}

// 在Channel所属的EventLoop中，把当前的channel删除
void Channel::remove()
{
    loop_->removeChannel(this);
}

// fd得到poller通知以后，用来处理事件的
void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_)
    {
        std::shared_ptr<void> guard = tie_.lock();
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
    }
    else
    {
        handleEventWithGuard(receiveTime);
    }
}

// 根据Poller通知Channel发生的具体事件，由Channel负责调用具体的回调操作
void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO("channel handleEvent revents : %d\n", revent_);

    if ((revent_ & EPOLLHUP) && !(revent_ & EPOLLIN))
    {
        if (closeCallback_)
        {
            closeCallback_();
        }
    }

    if (revent_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }

    if (revent_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }

    if (revent_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}
