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
# 装上 OpenCV 后会自动启用 bitmap/ 子项目，实现位图编辑和抠图。
# 没装时只给一条提示，不影响其余功能编译。
#
# 【版本约定】全组统一用 OpenCV 4.14（Windows 官方预编译包，MSVC 版本）。
# 这条要求来自技术指导会议「统一约定 OpenCV 库的使用版本」，理由：
#   · 官方预编译包只有 MSVC 版，MinGW 链不上（.lib 格式 + 名字改编不兼容）
#   · opencv_world4xx.lib 的文件名里带版本号，换版本就要同步改链接
#   · 团队里有人用 3.x 有人用 4.x 时，cv::floodFill 等函数的行为差异会导致
#     "我这能跑你那崩" 这类难查的问题
#
# 所以这里显式要求 4.x，并且建议 4.10 以上（我们实测的是 4.14.0）。
# 版本对不上时给一条醒目的警告，而不是等到运行时报找不到 DLL。
set(PAINTQT_OPENCV_MIN_VERSION "4.10")

find_package(OpenCV QUIET)
if(OpenCV_FOUND)
    message(STATUS "找到 OpenCV ${OpenCV_VERSION}：${OpenCV_DIR}")

    if(OpenCV_VERSION VERSION_LESS ${PAINTQT_OPENCV_MIN_VERSION})
        message(WARNING
            "OpenCV 版本过低：当前 ${OpenCV_VERSION}，本项目约定不低于 ${PAINTQT_OPENCV_MIN_VERSION}。\n"
            "位图功能仍会编译，但可能与团队其他成员的行为不一致。")
    else()
        message(STATUS "OpenCV 版本符合约定（>= ${PAINTQT_OPENCV_MIN_VERSION}）")
    endif()
else()
    message(STATUS "未找到 OpenCV —— 位图编辑/抠图功能将不参与编译")
endif()

# ---------------------------------------------------------------------------
#  paintqt_deploy_runtime(<target>)
#
#  构建完把 OpenCV 的运行时 DLL 拷到 exe 旁边。
#  OpenCV 装在独立目录里，不拷的话每次运行都得手工往 PATH 里加
#  D:\opencv\build\x64\vc16\bin，漏一次就是 "找不到 opencv_world4140d.dll"。
#
#  Qt 的 DLL 这里不自动部署，原因：
#    windeployqt 在这套 Qt 上会把 C:\WINDOWS\system32\icuuc.dll 当成 Qt 依赖
#    去拷贝（Qt 自己的 bin 里并没有 ICU），一旦目标文件被占用就报错，
#    还会把整个构建判成失败。开发阶段用 PATH 指向 Qt 的 bin 就够了，
#    要打包发布时手工跑一次 windeployqt 即可（见 README）。
# ---------------------------------------------------------------------------
function(paintqt_deploy_runtime target)
    if(NOT WIN32)
        return()
    endif()

    # ---- OpenCV：只拷当前配置对应的那一个（Debug 版带 d 后缀）----
    if(OpenCV_FOUND)
        file(GLOB _cv_bins "${OpenCV_DIR}/x64/*/bin/opencv_world*.dll")
        foreach(_dll ${_cv_bins})
            get_filename_component(_stem "${_dll}" NAME_WE)
            if(CMAKE_BUILD_TYPE STREQUAL "Debug")
                if(NOT _stem MATCHES "d$")
                    continue()
                endif()
            else()
                if(_stem MATCHES "d$")
                    continue()
                endif()
            endif()

            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:${target}>"
                COMMENT "部署 ${_stem}.dll -> ${target}"
                VERBATIM)
        endforeach()
    endif()
endfunction()

add_subdirectory(draw)      # 图形数据静态库（不依赖任何界面框架）
add_subdirectory(qtcanvas)  # Qt 画布封装静态库（QGraphicsItem 自定义图元）
add_subdirectory(svgio)     # SVG 解析静态库
add_subdirectory(dxfio)     # DXF 解析静态库

# 位图处理静态库（依赖 OpenCV）。没装 OpenCV 时整块跳过，
# 其余功能仍能正常编译运行。
if(OpenCV_FOUND)
    add_subdirectory(bitmap)
endif()

add_subdirectory(APP)       # 主程序，链接上面几个库
