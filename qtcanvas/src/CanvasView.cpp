#include "CanvasView.h"

#include <QLineF>
#include <QPainter>
#include <QWheelEvent>

namespace {
constexpr qreal kMinZoom = 0.2;
constexpr qreal kMaxZoom = 8.0;
constexpr int   kGridStep = 20;
} // namespace

CanvasView::CanvasView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);

    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setSelectMode(false);
}

void CanvasView::setSelectMode(bool on)
{
    setDragMode(on ? QGraphicsView::RubberBandDrag : QGraphicsView::NoDrag);
    setCursor(on ? Qt::ArrowCursor : Qt::CrossCursor);
}

void CanvasView::resetZoom()
{
    resetTransform();
    m_zoom = 1.0;
    emit zoomChanged(m_zoom);
}

void CanvasView::fitPage()
{
    fitInView(sceneRect(), Qt::KeepAspectRatio);

    // fitInView 直接改了变换矩阵，这里把缩放值同步回来，状态栏才不会显示错的数字
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
}

void CanvasView::drawBackground(QPainter* p, const QRectF& rect)
{
    const QRectF page = sceneRect();

    // 纸张外画浅灰，纸张内画白，这样边界一眼看得出来
    p->fillRect(rect, QColor(0xE9, 0xEC, 0xF1));
    p->fillRect(page, Qt::white);

    // 缩得太小就不画网格了，省性能
    if (m_zoom >= 0.5)
    {
        const QRectF g = rect.intersected(page);

        if (!g.isEmpty())
        {
            QPen grid(QColor(0xEE, 0xF1, 0xF6));
            grid.setWidth(0);                   // cosmetic pen，缩放时粗细不变
            p->setPen(grid);

            const int x0 = int(g.left()) / kGridStep * kGridStep;
            const int y0 = int(g.top()) / kGridStep * kGridStep;

            for (int x = x0; x <= int(g.right()) + kGridStep; x += kGridStep)
                p->drawLine(QLineF(x, g.top(), x, g.bottom()));

            for (int y = y0; y <= int(g.bottom()) + kGridStep; y += kGridStep)
                p->drawLine(QLineF(g.left(), y, g.right(), y));
        }
    }

    // 纸张边框
    QPen border(QColor(0xC9, 0xD1, 0xDB));
    border.setWidth(0);
    p->setPen(border);
    p->setBrush(Qt::NoBrush);
    p->drawRect(page);
}

void CanvasView::wheelEvent(QWheelEvent* e)
{
    // 不按 Ctrl 时保持默认的滚动行为
    if (!(e->modifiers() & Qt::ControlModifier))
    {
        QGraphicsView::wheelEvent(e);
        return;
    }

    const qreal factor = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
    const qreal next = m_zoom * factor;

    if (next < kMinZoom || next > kMaxZoom)
        return;

    m_zoom = next;
    scale(factor, factor);
    emit zoomChanged(m_zoom);
    e->accept();
}
