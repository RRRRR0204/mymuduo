#include "Poller.h"
#include "Channel.h"

Poller::Poller(EventLoop* loop)
    : ownerLoop_(loop)
{
}

bool Poller::hasChannel(Channel* channel) const
{
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

// 这里并没有实现newDefaultPoller这个方法（在外部公共地方实现），因为基类最好不要依赖于派生类