#pragma once

#include <iostream>
#include <string>
#include <stdint.h>

// 日志类
class Timestamp
{
public:
    Timestamp();
    explicit Timestamp(int64_t microSecondsSinceEpoch);
    static Timestamp now();
    std::string toString() const;

private:
    int64_t microSecondsSinceEpoch_;
};