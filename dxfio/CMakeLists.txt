cmake_minimum_required(VERSION 3.16)

# DXF 矢量文件导入静态库
#
# 把 DXF 的组码/值文本解析成 draw::Shape 列表，导入后是真正可编辑的图形对象
# （不是把文件渲染成一张图）。只依赖 Qt6::Core，不引入界面框架。
#
# 头文件也列在源文件里：AUTOMOC 只扫描目标列出的文件。
add_library(dxfio STATIC
    include/DxfImport.h
    src/DxfImport.cpp
)

target_include_directories(dxfio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dxfio PUBLIC
    draw
    Qt6::Core
)
