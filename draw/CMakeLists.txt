cmake_minimum_required(VERSION 3.16)

# 独立绘图类子项目，编译成静态库
#
# 头文件一并列出：GameObject / DrawingObject 是文档要求的继承体系的根，
# 放在源文件列表里 IDE 里也能直接看到。
add_library(draw STATIC
    include/Draw.h
    include/GameObject.h
    include/DrawingObject.h
    src/Draw.cpp
)

target_include_directories(draw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(draw PUBLIC cxx_std_17)
