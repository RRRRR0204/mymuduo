#pragma once

#include <vector>
#include <cstddef>
#include <string>
#include <algorithm>

// 网络库底层的缓冲区类型定义
/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize)
        , readerIndex_(kCheapPrepend)
        , writerIndex_(kCheapPrepend)
    {
    }

    size_t readableBytes() const
    {
        return writerIndex_ - readerIndex_;
    }

    size_t writableBytes() const
    {
        return buffer_.size() - writerIndex_;
    }

    size_t prependableBytes() const
    {
        return readerIndex_;
    }

    // onMessage : Buffer -> string
    void retrieve(size_t len)
    {
        if (len < readableBytes())
        {
            readerIndex_ += len;
        }
        else    // len == readableBytes()
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readerIndex_ = writerIndex_ = kCheapPrepend;
    }

    // 把onMessgae上报的Buffer数据 转成string类型的数据返回
    std::string retreiveAllAsString() 
    {
        return retrieveAsString(readableBytes());
    }

    std::string retrieveAsString(size_t len) 
    {
        std::string result(peek(), len);
        retrieve(len);     // 对缓冲区进行复位操作
        return result;
    }

    void ensureWritableBytes(size_t len)
    {
        if (writableBytes() < len)
        {
            makeSpace(len);  // 扩容函数
        }
    }

    // 把[data, data + len]内存上的数据添加到writable缓冲区中
    void append(const char* data, size_t len)
    {
        ensureWritableBytes(len);
        std::copy(data, data + len, beginWrite());
        writerIndex_ += len;
    }

    char* beginWrite()
    {
        return begin() + writerIndex_;
    }

    const char* beginWrite() const
    {
        return begin() + writerIndex_;
    }

    // 从fd上读数据
    ssize_t readFd(int fd, int* savedErrno);
    // 通过fd发送数据
    ssize_t writeFd(int fd, int* savedErrno);

private:
    // it.operator*()
    char *begin()
    {
        return &*buffer_.begin();
    } // vector底层数组首元素的地址，即数组的起始地址

    const char *begin() const
    {
        return &*buffer_.begin();
    }

    // 返回缓冲区中可读数据的起始地址
    const char *peek() const
    {
        return begin() + readerIndex_;
    }

    void makeSpace(size_t len)
    {
        /*
            kCheapPrepend |---|reader | writer |
            kCheapPrepend |           len             |
        */
       if (writableBytes() + prependableBytes() < len + kCheapPrepend)
       {
            buffer_.resize(writerIndex_ + len);
       }
       else
       {
            size_t readable = readableBytes();
            std::copy(begin() + readerIndex_,
                      begin() + writerIndex_,
                      begin() + kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable; 
       }
    }


    std::vector<char> buffer_;
    size_t readerIndex_;
    size_t writerIndex_;
};