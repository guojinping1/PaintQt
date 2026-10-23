#pragma once

#include <QByteArray>
#include <QString>

#include <vector>

#include "Draw.h"

// SVG 导入模块：把 SVG 文件解析成 draw 库的图形列表。
//
// 做的是**真解析**，不是把 SVG 渲染成图片 —— 所以导入后的线条
// 在画布上是真正的图形对象，可以继续编辑、撤销。
//
// 依赖 Qt6::Core（只用 QXmlStreamReader 读 XML），不依赖界面框架。
namespace svgio {

struct Result
{
    bool  ok = false;
    QString error;                      // 出错时的说明
    std::vector<draw::Shape> shapes;    // 解析出来的图形
    double width = 0, height = 0;       // SVG 声明的画布尺寸
    int   skipped = 0;                  // 跳过了几个不认识/不支持的元素
    int   arcs = 0;                     // 有几段圆弧被简化成了直线
};

Result loadFile(const QString& fileName);
Result loadData(const QByteArray& data);

} // namespace svgio
