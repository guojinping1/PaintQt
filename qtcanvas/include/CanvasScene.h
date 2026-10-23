#pragma once

#include <QGraphicsScene>
#include <QList>
#include <QPointF>
#include <QVector>

#include <vector>

#include "Draw.h"

class ShapeItem;

// 画布场景：管所有 ShapeItem，负责"当前用什么工具、从哪儿按下、拖出什么图形"。
// 撤销和清空直接操作场景里的图元，图形数据仍由 draw::Shape 保存。
class CanvasScene : public QGraphicsScene
{
    Q_OBJECT

public:
    enum class Tool
    {
        Select,     // 选择 + 节点编辑
        Line,       // 直线
        Rect,       // 矩形
        Ellipse,    // 椭圆
        Triangle,   // 三角形
        Bezier,     // 贝塞尔曲线
        Pencil,     // 铅笔
        Eraser      // 橡皮擦
    };
    Q_ENUM(Tool)

    explicit CanvasScene(QObject* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return m_tool; }

    void     setPenColor(unsigned c) { m_color = c; }
    unsigned penColor() const { return m_color; }

    void setPenWidth(int w);
    int  penWidth() const { return m_penWidth; }

    bool undo();            // 撤销最后一个图形
    void clearAll();        // 清空画布
    int  deleteSelected();  // 删除选中的图形，返回删掉几个

    int itemCount() const { return int(m_items.size()); }

    // 把外部图形（如 SVG 导入）加进场景。fitToView 为真时自动缩放居中
    void addShapes(const std::vector<draw::Shape>& shapes, bool fitToView);

signals:
    void countChanged(int n);
    void statusMessage(const QString& msg);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    ShapeItem* beginShape(const QPointF& p);
    void       updateShape(const QPointF& p);
    void       finishShape(const QPointF& p);

    void emitCount();

    Tool     m_tool = Tool::Line;
    unsigned m_color = 0x000000;
    int      m_penWidth = 2;

    bool             m_drawing = false;
    QPointF          m_start;
    ShapeItem*       m_temp = nullptr;
    QVector<QPointF> m_strokePts;       // 铅笔/橡皮擦的轨迹（用于抽稀）

    QList<ShapeItem*> m_items;          // 按加入顺序保存，撤销要用
};
