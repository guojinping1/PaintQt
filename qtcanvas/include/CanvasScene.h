#pragma once

#include <QGraphicsScene>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QVector>

#include <vector>

#include "Draw.h"

class ShapeItem;
class BitmapItem;

// 画布场景：管所有图元，负责"当前用什么工具、从哪儿按下、拖出什么图形"。
// 撤销和清空直接操作场景里的图元，矢量图形数据仍由 draw::Shape 保存。
//
// 场景里有两类图元：
//   ShapeItem   矢量图形（直线/矩形/贝塞尔…），数据在 draw::Shape 里
//   BitmapItem  位图（打开的图片、抠图结果）
// 两张都按加入顺序记在 m_order 里，所以撤销对两类都有效。
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
        Eraser,     // 橡皮擦
        Wand        // 魔棒抠图：在位图上点一下取种子点
    };
    Q_ENUM(Tool)

    explicit CanvasScene(QObject* parent = nullptr);

    void setTool(Tool t);
    Tool tool() const { return m_tool; }

    void     setPenColor(unsigned c) { m_color = c; }
    unsigned penColor() const { return m_color; }

    void setPenWidth(int w);
    int  penWidth() const { return m_penWidth; }

    bool undo();            // 撤销最后加入的一个图元（矢量或位图）
    void clearAll();        // 清空画布
    int  deleteSelected();  // 删除选中的图元，返回删掉几个

    // 状态栏显示用：矢量图形个数
    int shapeCount() const { return m_shapeCount; }

    // 把外部图形（如 SVG 导入）加进场景。fitToView 为真时自动缩放居中
    void addShapes(const std::vector<draw::Shape>& shapes, bool fitToView);

    // ---------------- 位图 ----------------

    // 加一张位图到画布中央并选中它
    BitmapItem* addBitmap(const QImage& img);

    // 当前选中的位图；没有选中的就返回最后加进来的那张
    BitmapItem* currentBitmap() const;

signals:
    void countChanged(int n);
    void statusMessage(const QString& msg);

    // 魔棒工具在位图上点了一下：item 是被点的位图，pixel 是图片像素坐标
    void wandPicked(BitmapItem* item, const QPoint& pixel);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    ShapeItem* beginShape(const QPointF& p);
    void       updateShape(const QPointF& p);
    void       finishShape(const QPointF& p);

    void track(QGraphicsItem* item);   // 加入顺序表（撤销要用）
    void untrack(QGraphicsItem* item);
    void emitCount();

    Tool     m_tool = Tool::Line;
    unsigned m_color = 0x000000;
    int      m_penWidth = 2;

    bool             m_drawing = false;
    QPointF          m_start;
    ShapeItem*       m_temp = nullptr;
    QVector<QPointF> m_strokePts;       // 铅笔/橡皮擦的轨迹（用于抽稀）

    QList<QGraphicsItem*> m_order;      // 所有图元，按加入顺序（撤销/清空用）
    BitmapItem*           m_lastBitmap = nullptr;
    int                   m_shapeCount = 0;   // 矢量图形个数
};
