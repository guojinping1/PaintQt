cmake_minimum_required(VERSION 3.16)

# SVG 导入模块：把 SVG 解析成 draw 库的图形
# 只依赖 Qt6::Core（读 XML 用），不依赖界面框架
add_library(svgio STATIC
    src/SvgImport.cpp
)

target_include_directories(svgio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(svgio PUBLIC
    draw
    Qt6::Core
)
