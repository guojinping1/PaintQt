cmake_minimum_required(VERSION 3.16)
project(PaintQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 可执行文件统一输出到 build/bin 下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC 默认按系统 ANSI 代码页读源文件（中文机器上是 GBK），
# 而本工程的源文件是 UTF-8 无 BOM。不加 /utf-8 的话中文字符串会乱码，
# 严重时双字节字符还会吃掉后面的引号导致编译错误。
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
qt_standard_project_setup()

# ---- OpenCV（可选）----
# 装上 OpenCV 后会自动启用 opencv/ 子项目，实现位图编辑和抠图。
# 没装时只给一条提示，不影响其余功能编译。
find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    message(STATUS "找到 OpenCV ${OpenCV_VERSION}：${OpenCV_DIR}")
else()
    message(STATUS "未找到 OpenCV —— 位图编辑/抠图功能将不参与编译")
endif()

add_subdirectory(draw)      # 图形数据静态库（不依赖任何界面框架）
add_subdirectory(qtcanvas)  # Qt 画布封装静态库（QGraphicsItem 自定义图元）
add_subdirectory(svgio)     # SVG 解析静态库
add_subdirectory(APP)       # 主程序，链接上面三个库
