cmake_minimum_required(VERSION 3.16)

# 主程序：左右分栏 —— 左侧功能区（图标按钮），右侧画布
# 界面全部用代码写，工程里没有任何 .ui 文件
qt_add_executable(APP
    main.cpp
)

set_target_properties(APP PROPERTIES
    WIN32_EXECUTABLE ON          # 不带控制台窗口
)

# 图标资源编进 exe，运行时用 :/icons/xxx.svg 访问。
# 注意：这里必须用 qt_add_resources，不能只把 .qrc 列进源文件——
# qt_standard_project_setup() 不会打开 AUTORCC，.qrc 会被静默忽略。
qt_add_resources(APP "app_icons"
    PREFIX "/icons"
    BASE   "icons"
    FILES
        icons/select.svg
        icons/line.svg
        icons/rect.svg
        icons/ellipse.svg
        icons/triangle.svg
        icons/bezier.svg
        icons/pencil.svg
        icons/eraser.svg
        icons/undo.svg
        icons/trash.svg
        icons/import.svg
        icons/save.svg
        icons/image.svg
        icons/gray.svg
        icons/edge.svg
        icons/wand.svg
        icons/reset.svg
        icons/dxf.svg
)

target_link_libraries(APP PRIVATE
    draw
    qtcanvas
    svgio
    dxfio
    Qt6::Widgets
    Qt6::Svg                     # SVG 图标要靠它加载
)

# 位图功能只有在找到 OpenCV 时才编进来。
# 没装 OpenCV 时 APP 照常编译运行，只是左侧不显示「位图」那一栏。
if(OpenCV_FOUND)
    target_link_libraries(APP PRIVATE bitmap)
    target_compile_definitions(APP PRIVATE PAINTQT_HAVE_OPENCV)
endif()

paintqt_deploy_runtime(APP)
