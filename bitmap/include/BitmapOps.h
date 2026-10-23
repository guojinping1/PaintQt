#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>

// ============================================================================
//  位图处理库（基于 OpenCV）
//
//  设计原则：对外只暴露 Qt 类型（QImage），OpenCV 是纯内部实现细节。
//  这样上层（qtcanvas / APP）不需要 include 任何 OpenCV 头文件，
//  也不需要知道 cv::Mat 的存在。将来换别的图像库，只改这个 .cpp。
//
//  所有函数都是"输入 QImage → 输出新 QImage"，不修改入参，
//  所以撤销/重做只要保存旧的那张图就行。
// ============================================================================
namespace bitmap {

// ---------------- 位图编辑 ----------------

// 灰度
QImage toGray(const QImage& src);

// 二值化：threshold 取 0~255，大于它的变白、其余变黑
QImage toBinary(const QImage& src, int threshold);

// 高斯模糊：radius 越大越糊（内部换算成奇数核）
QImage gaussianBlur(const QImage& src, int radius);

// Canny 边缘检测，输出黑白线条图
QImage edgeDetect(const QImage& src, int lowThreshold, int highThreshold);

// 亮度调整：delta 取 -100~100
QImage adjustBrightness(const QImage& src, int delta);

// 反色
QImage invert(const QImage& src);

// ---------------- 抠图 ----------------

// 魔棒抠图：从 seed 点开始，把颜色相近的连通区域保留下来，其余变透明。
//
//   tolerance        颜色容差，0 表示只选完全相同的颜色，越大选得越多
//   outMaskBounds    返回选中区域的外接矩形（原图坐标系），方便上层裁剪
//
// 返回的是**和原图一样大**的透明底图，只有选中区域有像素。
QImage magicWand(const QImage& src, const QPoint& seed, int tolerance,
                 QRect* outMaskBounds = nullptr);

// 把四周全透明的边裁掉，只留有效内容
QImage trimTransparent(const QImage& src, QRect* outBounds = nullptr);

// 抠图结果的反向：保留选区之外的像素
QImage magicWandInvert(const QImage& src, const QPoint& seed, int tolerance);

} // namespace bitmap
