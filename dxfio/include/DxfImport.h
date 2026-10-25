#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

#include "Draw.h"

// DXF 导入模块：把 DXF 文件解析成 draw 库的图形列表。
//
// 和 svgio 一样，做的是**真解析**而不是把文件渲染成图片 ——
// 导入后在画布上是真正的图形对象，可以继续编辑、撤销。
//
// 只支持 ASCII 版 DXF（组码/值成对出现的那种文本格式）。
// 二进制 DXF 和 DWG 不支持。
//
// 支持的元素：
//   LINE            → 直线
//   LWPOLYLINE      → 多段线
//   POLYLINE+VERTEX → 多段线（老式写法，顶点是独立实体）
//   CIRCLE          → 椭圆（外接矩形）
//   ARC             → 用折线逼近的圆弧
//   ELLIPSE         → 用折线逼近的椭圆/椭圆弧
//   INSERT          → 图块引用（平移 + 缩放，不含旋转）
//   LAYER 表        → 图层颜色（ACI 索引色）
//
// 依赖 Qt6::Core（QString/QByteArray），不依赖界面框架。
namespace dxfio {

struct Result
{
    bool  ok = false;
    QString error;                      // 出错时的说明
    std::vector<draw::Shape> shapes;    // 解析出来的图形
    int   skipped = 0;                  // 跳过了几个不支持的元素
    int   curves = 0;                   // 有几条曲线被离散成了折线
    int   blocks = 0;                   // 展开了几个图块
};

Result loadFile(const QString& fileName);
Result loadData(const QByteArray& data);

} // namespace dxfio
