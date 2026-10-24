#include "BitmapItem.h"

#include <QPixmap>

#include <cmath>

BitmapItem::BitmapItem(const QImage& img, QGraphicsItem* parent)
    : QGraphicsPixmapItem(parent), m_source(img), m_current(img)
{
    setFlags(ItemIsSelectable | ItemIsMovable);
    setTransformationMode(Qt::SmoothTransformation);   // 缩放时平滑，不要马赛克

    refreshPixmap();
}

void BitmapItem::refreshPixmap()
{
    setPixmap(QPixmap::fromImage(m_current));
}

void BitmapItem::applyFilter(const QImage& result)
{
    if (result.isNull())
        return;

    m_current = result;
    refreshPixmap();
}

void BitmapItem::resetToSource()
{
    m_current = m_source;
    refreshPixmap();
}

QPoint BitmapItem::toImagePixel(const QPointF& scenePos) const
{
    // 先换到图元局部坐标，再向下取整到像素
    const QPointF p = mapFromScene(scenePos);
    return QPoint(int(std::floor(p.x())), int(std::floor(p.y())));
}
