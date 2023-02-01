#include "EventLoop.h"
#include "Logger.h"
#include "Channel.h"
#include "Poller.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

// 防止一个线程创建多个EventLoop
__thread EventLoop *t_LoopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000;

// 创建wakeupFd，用来notify唤醒subReactor处理新来的Channel
int createEventFd()
{
    int evtFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtFd < 0)
    {
        LOG_FATAL("eventfd error:%d \n", errno);
    }
    return evtFd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventFd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
    , currentActiveChannel_(nullptr)
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_LoopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_LoopInThisThread, threadId_);
    }
    else
    {
        t_LoopInThisThread = this;
    }

    // 设置wakeupFd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个EventLoop都将监听wakeupChannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupFd_);
    t_LoopInThisThread = nullptr;
}

void EventLoop::handleRead()
{
    uint64_t one = 1;
    ssize_t n = read(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::handleRead() reads %d bytes instead of 8\n", n);
    }
}

// 用来唤醒loop所在的线程，向wakeupFd写一个数据，wakeupChannel发生读事件，当前loop线程就会被唤醒
void EventLoop::wakeup()
{
    uint64_t one = 1;
    ssize_t n = write(wakeupFd_, &one, sizeof one);
    if (n != sizeof one)
    {
        LOG_ERROR("EventLoop::wakeup() writes %lu bytes instead of 8\n", n);
    }
}

// 开启事件循环
void EventLoop::loop()
{   
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while (!quit_)
    {
        activeChannels_.clear();
        // 如果是subLoop，监听两类fd：（1）client的fd   （2）wakeupFd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些Channel发生事件了，上报给EventLoop，通知Channel处理相应的事件
            currentActiveChannel_ = channel;
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        /**
         * mainLoop：accept新连接 => fd（封装成channel）交给subLoop
         * subLoop如何收到？通过eventfd，统一事件源
         *              mainLoop事先注册一个回调cb（需要subLoop来执行），wakeup后上面的操作只是执行一下handleRead
         *              真正处理的操作是下面的函数
        */
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping \n", this);
    looping_ = false;
}

// 退出事件循环
// 两种情况：（1）loop在自己的线程中调用quit    
//          （2）在非loop的线程中，调用loop的quit（这时就需要通知到要quit的loop，从阻塞poll中唤醒）
void EventLoop::quit()
{
    quit_ = true;

    if (!isInLoopThread())
    {
        wakeup();      // 在其他线程中调用quit，就需要通知该loop，将其唤醒
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread())     // 在当前loop线程中，执行cb
    {
        cb();
    }
    else                    // 在非当前loop线程中执行，Loop_1中：Loop_2.runInLoop(cb)，就需要唤醒mainLoop所在线程
    {
        queueInLoop(cb);
    }
}

// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop线程
    // 这里的||callingPendingFunctors_需要好好想想：当前的loop正在执行回调，这时又向里面添加了新的functor
    //                                          需要给loop写一个消息，让他在下轮poll跳出继续执行回调
    if (!isInLoopThread() || callingPendingFunctors_)   
    {
        wakeup();   // 唤醒loop所在线程 
    }
}

// 调用Poller的方法
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel);
}

// 执行回调
void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors;
    callingPendingFunctors_ = true;

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_);
    }

    for (const Functor& functor : functors)
    {
        functor();  // 执行当前loop需要执行的回调操作
    }
    callingPendingFunctors_ = false;
}