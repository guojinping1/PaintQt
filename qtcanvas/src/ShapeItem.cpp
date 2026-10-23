#include "ShapeItem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

#include "QtRenderer.h"

namespace {

// 把图形转成 QPainterPath。
// 用途：点选判定（shape()）和选中时的控制多边形提示。
// 真正画图走的是 m_object.render() → draw::renderShape + QtRenderer。
QPainterPath pathOf(const draw::Shape& s)
{
    QPainterPath p;

    const int l = std::min(s.x1, s.x2);
    const int t = std::min(s.y1, s.y2);
    const int r = std::max(s.x1, s.x2);
    const int b = std::max(s.y1, s.y2);

    const QRectF box(l, t, r - l, b - t);

    switch (s.type)
    {
    case draw::ShapeType::Line:
        p.moveTo(s.x1, s.y1);
        p.lineTo(s.x2, s.y2);
        break;

    case draw::ShapeType::Rect:
        p.addRect(box);
        break;

    case draw::ShapeType::Ellipse:
        p.addEllipse(box);
        break;

    case draw::ShapeType::Triangle:
        p.moveTo((l + r) / 2.0, t);
        p.lineTo(r, b);
        p.lineTo(l, b);
        p.closeSubpath();
        break;

    case draw::ShapeType::Bezier:
        if (s.pts.size() >= 4)
        {
            p.moveTo(s.pts[0].x, s.pts[0].y);
            p.cubicTo(s.pts[1].x, s.pts[1].y, s.pts[2].x, s.pts[2].y, s.pts[3].x, s.pts[3].y);
        }
        break;

    case draw::ShapeType::Stroke:
    case draw::ShapeType::Polyline:
        if (!s.pts.empty())
        {
            p.moveTo(s.pts[0].x, s.pts[0].y);
            for (size_t i = 1; i < s.pts.size(); ++i)
                p.lineTo(s.pts[i].x, s.pts[i].y);
            if (s.closed)
                p.closeSubpath();
        }
        break;
    }

    return p;
}

} // namespace

ShapeItem::ShapeItem(const draw::Shape& s, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_object(s)
{
    // 可选中、可整体拖动；拖动时通知场景
    setFlags(ItemIsSelectable | ItemIsMovable | ItemSendsGeometryChanges);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void ShapeItem::setShape(const draw::Shape& s)
{
    // prepareGeometryChange 必须在改数据之前调，否则 QGraphicsScene 的
    // BSP 索引会失效，表现为残影和点选错位
    prepareGeometryChange();
    m_object.setShape(s);
    update();
}

QRectF ShapeItem::boundingRect() const
{
    // 边界直接问 GameObject 要，ShapeItem 不重复算一遍
    const draw::Bounds b = m_object.bounds();

    if (!b.valid)
        return QRectF(-1, -1, 2, 2);

    // 留出线宽和节点手柄的余量，否则手柄会被裁掉
    const qreal m = kHandleRadius + std::max<qreal>(m_object.shape().width, 1.0) + 3.0;

    return QRectF(b.left - m,
                  b.top - m,
                  (b.right - b.left) + 2 * m,
                  (b.bottom - b.top) + 2 * m);
}

QPainterPath ShapeItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max<qreal>(m_object.shape().width, 10.0));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    QPainterPath hit = stroker.createStroke(pathOf(m_object.shape()));

    // 选中时把手柄也算进点选范围。
    // 贝塞尔的控制点离曲线很远，不加这段就拖不到。
    if (isSelected())
    {
        for (const QPointF& h : handles())
            hit.addEllipse(h, kHitRadius, kHitRadius);
    }

    return hit;
}

void ShapeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);

    // 画图形本体：交给 GameObject 的 render 接口，
    // 由 DrawingObject → draw::renderShape → QtRenderer 一路落地成 QPainter 调用
    QtRenderer renderer(*p);
    m_object.render(renderer);

    if (isSelected())
    {
        const QVector<QPointF> hs = handles();
        const draw::Shape& s = m_object.shape();

        // 贝塞尔：画出控制多边形（起点→控制点1，控制点2→终点）
        if (s.type == draw::ShapeType::Bezier && hs.size() >= 4)
        {
            QPen guide(QColor(0x90, 0xA4, 0xC0), 1.0, Qt::DashLine);
            p->setPen(guide);
            p->setBrush(Qt::NoBrush);
            p->drawLine(hs[0], hs[1]);
            p->drawLine(hs[2], hs[3]);
        }

        // 选中虚线框
        QPen sel(QColor(0x1E, 0x78, 0xDC), 1.0, Qt::DashLine);
        p->setPen(sel);
        p->setBrush(Qt::NoBrush);
        p->drawRect(boundingRect().adjusted(3, 3, -3, -3));

        // 节点手柄
        p->setPen(QPen(QColor(0x1E, 0x78, 0xDC), 1.5));
        p->setBrush(Qt::white);
        for (const QPointF& h : hs)
            p->drawEllipse(h, kHandleRadius, kHandleRadius);
    }

    p->restore();
}

// ---------------- 节点手柄 ----------------

QVector<QPointF> ShapeItem::handles() const
{
    QVector<QPointF> out;

    for (const draw::Point& q : draw::nodesOf(m_object.shape()))
        out << QPointF(q.x, q.y);

    return out;
}

int ShapeItem::handleAt(const QPointF& p) const
{
    const QVector<QPointF> hs = handles();

    for (int i = 0; i < hs.size(); ++i)
    {
        if (QLineF(hs[i], p).length() <= kHitRadius)
            return i;
    }

    return -1;
}

void ShapeItem::setHandlePos(int index, const QPointF& p)
{
    prepareGeometryChange();

    // 直接改 DrawingObject 里的 Shape —— moveNode 只关心数据，不关心对象
    if (draw::moveNode(m_object.shape(), index,
                       int(std::lround(p.x())), int(std::lround(p.y()))))
    {
        update();
    }
}

// ---------------- 鼠标交互 ----------------

void ShapeItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    // 已选中时，优先判定是不是按在节点手柄上
    if (isSelected())
    {
        m_dragHandle = handleAt(e->pos());
        if (m_dragHandle >= 0)
        {
            e->accept();
            return;
        }
    }

    // 否则交给基类做整体拖动 / 选中
    QGraphicsItem::mousePressEvent(e);
}

void ShapeItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_dragHandle >= 0)
    {
        setHandlePos(m_dragHandle, e->pos());
        e->accept();
        return;
    }

    QGraphicsItem::mouseMoveEvent(e);
}

void ShapeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    m_dragHandle = -1;
    QGraphicsItem::mouseReleaseEvent(e);
}
