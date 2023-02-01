#include "Socket.h"
#include "Logger.h"
#include "InetAddress.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <strings.h>

Socket::~Socket()
{
    close(sockfd_);
}

void Socket::bindAddress(const InetAddress &loacladdr)
{
    if (0 != ::bind(sockfd_, (sockaddr *)loacladdr.getSockAddr(), sizeof(sockaddr_in)))
    {
        LOG_FATAL("bind sockfd:%d fail \n", sockfd_);
    }
}

void Socket::listen()
{
    if (0 != ::listen(sockfd_, 1024))
    {
        LOG_FATAL("listen sockfd:%d fail \n", sockfd_);
    }
}

int Socket::accept(InetAddress *peeraddr)
{
    /**
     * bug:
     * 1. accept的参数不合法（len未初始化）
     * 2. 对返回的connfd没有设置非阻塞
     *  Reactor模型 one loop per thread
     *  poller + non-blocking io
    */
    sockaddr_in addr;
    socklen_t len = sizeof addr;
    bzero(&addr, sizeof addr);
    int connfd = ::accept4(sockfd_, (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0)
    {
        peeraddr->setSockAddr(addr);
    }
    return connfd;
}

void Socket::shutdownWrite()
{
    if (::shutdown(sockfd_, SHUT_WR))
    {
        LOG_ERROR("shutdownWrite error");
    }
}

void Socket::setTcpNoDelay(bool on)
{
    int opt = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt);
}

void Socket::setReuseAddr(bool on)
{
    int opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
}

void Socket::setReusePort(bool on)
{
    int opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof opt);
}

void Socket::setKeepAlive(bool on)
{
    int opt = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof opt);
}