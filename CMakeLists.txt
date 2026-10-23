cmake_minimum_required(VERSION 3.16)
project(PaintQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件统一输出到 build/bin 下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
qt_standard_project_setup()

add_subdirectory(draw)      # 图形数据静态库（不依赖任何界面框架）
add_subdirectory(qtcanvas)  # Qt 画布封装静态库（QGraphicsItem 自定义图元）
add_subdirectory(svgio)     # SVG 解析静态库
add_subdirectory(APP)       # 主程序，链接上面三个库
