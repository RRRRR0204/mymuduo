cmake_minimum_required(VERSION 3.21.0)
project(mymuduo)

# 脱离boost库的限制，使用C++11，最终编译成一个动态库
# 设置动态库的路径，放在根目录的lib文件夹下
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置调试信息，启动c++11标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++11")

# 定义参与编译的源文件
aux_source_directory(. SRC_LIST)
# 编译生成动态库mymuduo
add_library(mymuduo SHARED ${SRC_LIST})


# cmake -> makefile make