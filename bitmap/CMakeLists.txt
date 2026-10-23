cmake_minimum_required(VERSION 3.16)

# 位图处理静态库（基于 OpenCV）
# 对外只暴露 Qt 类型（QImage），OpenCV 是纯内部实现细节
add_library(bitmap STATIC
    include/BitmapOps.h
    src/BitmapOps.cpp
)

target_include_directories(bitmap PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# OpenCV 的头文件只在实现里用到，所以是 PRIVATE ——
# 上层链接 bitmap 时不需要任何 OpenCV 的 include 路径
target_include_directories(bitmap PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(bitmap PUBLIC
    Qt6::Gui
)
target_link_libraries(bitmap PRIVATE
    ${OpenCV_LIBS}
)

# ---------------------------------------------------------------------------
# 自测程序：生成一张测试图，把每个功能都跑一遍并存成 PNG，
# 用来验证 OpenCV 的链接和运行是否正常，不需要开界面。
#
#   bitmap_selftest.exe <输出目录>
# ---------------------------------------------------------------------------
add_executable(bitmap_selftest
    src/SelfTest.cpp
)
target_link_libraries(bitmap_selftest PRIVATE bitmap)

set_target_properties(bitmap_selftest PROPERTIES
    WIN32_EXECUTABLE OFF      # 这是控制台程序，要能直接看到输出
)

# 把 Qt 和 OpenCV 的运行时 DLL 拷到 exe 旁边，这样直接双击就能跑，
# 不用手工往 PATH 里加 D:\opencv\build\x64\vc16\bin
paintqt_deploy_runtime(bitmap_selftest)
