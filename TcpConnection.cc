#include "TcpConnection.h"
#include "Logger.h"
#include "Socket.h"
#include "Channel.h"
#include "EventLoop.h"

#include <functional>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <strings.h>
#include <string>
#include <unistd.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL("%s:%s:%d TcpConnection Loop is null! \n", __FILE__, __FUNCTION__, __LINE__);
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                  const std::string nameArg,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64 * 1024 * 1024)  // 64M
{
    // 给Channel设置相应的回调，poller通知感兴趣的事件发生，Channel会执行相应的回调
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d \n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO("TcpConnection::dtor[%s] at fd=%d state=%d \n",
        name_.c_str(), channel_->fd(), (int)state_);
}

void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
    }
    else
    {
        loop_->runInLoop(
            std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size())
        );
    }
}

// 发送数据，应用写的快，内核发送数据慢，需要把发送数据写入缓冲区，而且设置了高水位回调
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t rwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前已经调用过该connection的shutdown，不能再发送了
    if (state_ == kDisConnected)
    {
        LOG_ERROR("disconnected, give up writing! \n");
        return;
    }
    // channel第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        rwrote = ::write(channel_->fd(), data, len);
        if (rwrote >= 0)
        {
            remaining = len - rwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 既然在这里直接一次性发送完了，就不需要再给channel设置epollout事件了
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else   // rwrote < 0
        {
            rwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET)
                {
                    faultError = true;
                }
            }
        }
    }

    // 当前这一次write并没有把数据发送完，剩余的数据需要存到缓冲区中，并给channel注册epollout事件
    // LT模式，poller发现tcp写缓冲区有空间，会一直上报epollout事件，调用writeCallback回调
    // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
    if (!faultError && remaining > 0)
    {
        // 目前缓冲区剩余待发送数据长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining > highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining)
            );
        }
        outputBuffer_.append((char*)data + rwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting();  // 注册channel的写事件  
        }
    }   
}

// 关闭连接
void TcpConnection::shutdown()
{
    if (state_ == kConnected)
    {
        setState(kDisConnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this)
        );
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting())         // 说明outputBuffer中的数据已经全部发送完毕
    {
        socket_->shutdownWrite();       // 关闭写端
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading();      // 向poller注册channel的EPOLLIN事件

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this());
}

// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisConnected);
        channel_->disableAll();     // 把channel所有的感兴趣事件，从poller中del掉
        connectionCallback_(shared_from_this());
    }

    channel_->remove();     // 把channel从poller中删掉
}


void TcpConnection::handleRead(Timestamp receiveTime)
{
    int saveErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &saveErrno);
    if (n > 0)
    {
        // 已建立连接的用户，有可读事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = saveErrno;
        LOG_ERROR("TcpConnection::handleRead \n");
        handleError();
    }
}

void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int saveErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &saveErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 下面这样写也行，loop_肯定就是subloop
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }
                if (state_ == kDisConnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite \n");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing \n", channel_->fd());
    }
}

// poller => channel::closeCallback => TcpConnection::handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(), (int)state_);
    setState(kDisConnected);
    channel_->disableAll();
 
    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr);   // 执行连接关闭的回调
    closeCallback_(connPtr);    // 关闭连接的回调，执行的是TcpServer::removeConnection
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), err);
}