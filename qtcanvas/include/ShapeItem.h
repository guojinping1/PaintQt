#pragma once

#include <QGraphicsItem>
#include <QVector>

#include "DrawingObject.h"

// 继承 QGraphicsItem 的自定义图元，代表画布上的一个图形。
//
//   · 单击选中，选中后四角/端点出现圆形节点手柄
//   · 拖动节点手柄 → 编辑图形形状（直线改端点、矩形改对角、贝塞尔改控制点）
//   · 拖动图形本体 → 整条平移（QGraphicsItem 自带的 ItemIsMovable）
//
// 【和 GameObject 的关系】
//   图形数据封装在 draw::DrawingObject 里（它继承 draw::GameObject），
//   ShapeItem 只负责"Qt 这一侧"的事：命中判定、鼠标交互、选中框和手柄的绘制。
//   三层分工：
//       draw::GameObject    → 对象接口（边界、渲染、id、图层）
//       draw::DrawingObject → 绘图功能类（持有一个 Shape），文档要求的那个类
//       ShapeItem           → Qt 适配层（QGraphicsItem 子类）
class ShapeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };
    int type() const override { return Type; }

    // 节点手柄半径 / 点选判定半径
    static constexpr qreal kHandleRadius = 4.5;
    static constexpr qreal kHitRadius = 9.0;

    explicit ShapeItem(const draw::Shape& s, QGraphicsItem* parent = nullptr);

    // 取图形数据。注意不能叫 shape()，那会和
    // QGraphicsItem::shape()（返回 QPainterPath 的点选路径）撞名
    const draw::Shape& drawShape() const { return m_object.shape(); }
    void setShape(const draw::Shape& s);

    // 底层的 GameObject（文档要求的继承关系在这里体现）
    const draw::DrawingObject& object() const { return m_object; }
    draw::DrawingObject&       object()       { return m_object; }

    QRectF     boundingRect() const override;
    QPainterPath shape() const override;
    void       paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

    // 当前可拖拽的节点位置（图元局部坐标）
    QVector<QPointF> handles() const;
    int  handleAt(const QPointF& p) const;
    void setHandlePos(int index, const QPointF& p);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    draw::DrawingObject m_object;               // ← 继承 GameObject 的绘图功能类
    int                 m_dragHandle = -1;      // -1 表示没在拖节点
};
