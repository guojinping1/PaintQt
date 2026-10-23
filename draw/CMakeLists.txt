cmake_minimum_required(VERSION 3.16)

# 独立绘图类子项目，编译成静态库
add_library(draw STATIC
    src/Draw.cpp
)

target_include_directories(draw PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(draw PUBLIC cxx_std_17)
