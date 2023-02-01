#include "Acceptor.h"
#include "Logger.h"
#include "InetAddress.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

static int createNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        LOG_FATAL("%s:%s:%d listen socket create error:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
    }
}

Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking())    // socket
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false) 
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr);  // bind

    // TcpServer:start() -> Accept::listen()  有新用户连接时，要执行一个回调（connfd打包成channel，再给交给subLoop）
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
}

void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen(); // listen
    acceptChannel_.enableReading();
}

// listenfd有事件发生（新用户连接）
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr);   // 轮询找到subLoop，唤醒，分发当前新客户端的channel
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept error:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reach limit! \n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}