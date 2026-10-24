cmake_minimum_required(VERSION 3.16)

# Qt 画布封装静态库
#   ShapeItem   —— 继承 QGraphicsItem 的自定义图元（含节点拖拽编辑）
#   BitmapItem  —— 位图图元（打开的图片 / 抠图结果）
#   CanvasScene —— 场景，管工具状态和画图交互
#   CanvasView  —— 视图，白纸 + 网格 + 缩放
#
# 头文件必须一起列进来：AUTOMOC 只扫描目标源文件里的文件，
# 不列的话它找不到 Q_OBJECT，moc 不会生成，链接时报
# "undefined reference to XXX::staticMetaObject"。
add_library(qtcanvas STATIC
    include/QtRenderer.h
    include/ShapeItem.h
    include/BitmapItem.h
    include/CanvasScene.h
    include/CanvasView.h
    src/ShapeItem.cpp
    src/BitmapItem.cpp
    src/CanvasScene.cpp
    src/CanvasView.cpp
)

target_include_directories(qtcanvas PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(qtcanvas PUBLIC
    draw
    Qt6::Widgets
)