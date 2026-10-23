#include "CanvasScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#include "ShapeItem.h"

namespace {

int iround(qreal v)
{
    return int(std::lround(v));
}

} // namespace

CanvasScene::CanvasScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(0, 0, 1600, 1100);
    setBackgroundBrush(Qt::white);
}

void CanvasScene::setTool(Tool t)
{
    m_tool = t;
    clearSelection();               // 换工具就取消选中，避免节点手柄干扰
}

void CanvasScene::setPenWidth(int w)
{
    m_penWidth = qBound(1, w, 40);
}

// ---------------- 撤销 / 清空 ----------------

bool CanvasScene::undo()
{
    if (m_items.isEmpty())
    {
        emit statusMessage(QStringLiteral("没有可以撤销的图形"));
        return false;
    }

    ShapeItem* item = m_items.takeLast();
    removeItem(item);
    delete item;

    emitCount();
    emit statusMessage(QStringLiteral("已撤销"));
    return true;
}

void CanvasScene::clearAll()
{
    const int n = int(m_items.size());

    for (ShapeItem* item : m_items)
    {
        removeItem(item);
        delete item;
    }
    m_items.clear();

    emitCount();
    emit statusMessage(QStringLiteral("已清空 %1 个图形").arg(n));
}

int CanvasScene::deleteSelected()
{
    const QList<QGraphicsItem*> sel = selectedItems();
    int n = 0;

    for (QGraphicsItem* item : sel)
    {
        auto* si = qgraphicsitem_cast<ShapeItem*>(item);
        if (!si)
            continue;

        m_items.removeOne(si);
        removeItem(si);
        delete si;
        ++n;
    }

    if (n > 0)
    {
        emitCount();
        emit statusMessage(QStringLiteral("已删除 %1 个图形").arg(n));
    }

    return n;
}

// ---------------- 导入外部图形 ----------------

void CanvasScene::addShapes(const std::vector<draw::Shape>& shapes, bool fitToView)
{
    if (shapes.empty())
        return;

    std::vector<draw::Shape> data = shapes;

    if (fitToView)
    {
        const draw::Bounds b = draw::boundsOf(data);

        if (b.valid)
        {
            const double bw = b.right - b.left;
            const double bh = b.bottom - b.top;
            const QRectF area = sceneRect().adjusted(40, 40, -40, -40);

            double k = 4.0;                     // 小图最多放大 4 倍
            if (bw > 0)
                k = std::min(k, area.width() / bw);
            if (bh > 0)
                k = std::min(k, area.height() / bh);
            if (k <= 0)
                k = 1.0;

            const double ox = area.left() + (area.width() - bw * k) / 2.0;
            const double oy = area.top() + (area.height() - bh * k) / 2.0;

            auto move = [&](int& x, int& y)
            {
                const double nx = ox + (x - b.left) * k;
                const double ny = oy + (y - b.top) * k;
                x = iround(nx);
                y = iround(ny);
            };

            for (draw::Shape& s : data)
            {
                move(s.x1, s.y1);
                move(s.x2, s.y2);

                for (draw::Point& p : s.pts)
                    move(p.x, p.y);

                if (k > 1.0)
                    s.width = std::max(1, iround(s.width * k));
            }
        }
    }

    for (const draw::Shape& s : data)
    {
        auto* item = new ShapeItem(s);
        addItem(item);
        m_items.append(item);
    }

    emitCount();
}

void CanvasScene::emitCount()
{
    emit countChanged(int(m_items.size()));
}

// ---------------- 鼠标：画图 ----------------

void CanvasScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    // 选择工具交给基类，负责选中和整体拖动
    if (m_tool == Tool::Select || e->button() != Qt::LeftButton)
    {
        QGraphicsScene::mousePressEvent(e);
        return;
    }

    m_drawing = true;
    m_start = e->scenePos();
    m_temp = beginShape(m_start);
    e->accept();
}

void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (!m_drawing)
    {
        QGraphicsScene::mouseMoveEvent(e);
        return;
    }

    updateShape(e->scenePos());
    e->accept();
}

void CanvasScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (!m_drawing)
    {
        QGraphicsScene::mouseReleaseEvent(e);
        return;
    }

    finishShape(e->scenePos());
    e->accept();
}

ShapeItem* CanvasScene::beginShape(const QPointF& p)
{
    draw::Shape s;
    s.color = (m_tool == Tool::Eraser) ? 0xFFFFFF : m_color;    // 橡皮擦 = 白色笔迹
    s.width = (m_tool == Tool::Eraser) ? 22 : m_penWidth;

    const int x = iround(p.x());
    const int y = iround(p.y());

    switch (m_tool)
    {
    case Tool::Line:     s.type = draw::ShapeType::Line;     break;
    case Tool::Rect:     s.type = draw::ShapeType::Rect;     break;
    case Tool::Ellipse:  s.type = draw::ShapeType::Ellipse;  break;
    case Tool::Triangle: s.type = draw::ShapeType::Triangle; break;

    case Tool::Bezier:
        // 四个控制点先都放在按下的位置，拖动时再拉开
        s.type = draw::ShapeType::Bezier;
        s.pts.assign(4, draw::Point{ x, y });
        break;

    case Tool::Pencil:
    case Tool::Eraser:
        s.type = draw::ShapeType::Stroke;
        s.pts.push_back({ x, y });
        m_strokePts.clear();
        m_strokePts << p;
        break;

    default:
        break;
    }

    s.x1 = s.x2 = x;
    s.y1 = s.y2 = y;

    auto* item = new ShapeItem(s);
    addItem(item);
    return item;
}

void CanvasScene::updateShape(const QPointF& p)
{
    if (!m_temp)
        return;

    draw::Shape s = m_temp->drawShape();

    const int x = iround(p.x());
    const int y = iround(p.y());

    switch (m_tool)
    {
    case Tool::Line:
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Triangle:
        s.x2 = x;
        s.y2 = y;
        break;

    case Tool::Bezier:
    {
        // 拖出来的默认形状是个 S 形：
        // 控制点1 贴在起点高度、横向 1/3 处；控制点2 贴在终点高度、横向 2/3 处。
        // 画完之后可以拖控制点任意调整。
        const int x0 = s.pts[0].x;
        const int y0 = s.pts[0].y;

        s.pts[3] = { x, y };
        s.pts[1] = { x0 + (x - x0) / 3, y0 };
        s.pts[2] = { x0 + (x - x0) * 2 / 3, y };
        break;
    }

    case Tool::Pencil:
    case Tool::Eraser:
    {
        // 抽稀：移动不到 2 像素就不记点，省内存也不影响观感
        if (!m_strokePts.isEmpty())
        {
            const QPointF& last = m_strokePts.back();
            if (qAbs(p.x() - last.x()) + qAbs(p.y() - last.y()) < 2.0)
                return;
        }

        m_strokePts << p;
        s.pts.push_back({ x, y });
        break;
    }

    default:
        break;
    }

    m_temp->setShape(s);
}

void CanvasScene::finishShape(const QPointF& p)
{
    if (!m_temp)
        return;

    updateShape(p);

    const draw::Shape s = m_temp->drawShape();
    bool keep = false;

    switch (m_tool)
    {
    case Tool::Line:
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Triangle:
        keep = (s.x1 != s.x2 || s.y1 != s.y2);      // 原地单击就丢掉
        break;

    case Tool::Bezier:
        keep = (s.pts.size() >= 4) &&
               (s.pts[0].x != s.pts[3].x || s.pts[0].y != s.pts[3].y);
        break;

    default:
        keep = (s.pts.size() >= 2);
        break;
    }

    if (keep)
    {
        m_items.append(m_temp);
        emitCount();
        emit statusMessage(QStringLiteral("已添加图形，共 %1 个").arg(int(m_items.size())));
    }
    else
    {
        removeItem(m_temp);
        delete m_temp;
    }

    m_temp = nullptr;
    m_strokePts.clear();
    m_drawing = false;
}
