#pragma once

#include <QGraphicsPixmapItem>
#include <QImage>

// 画布上的一张位图。
//
// 同时保存「原图」和「当前显示图」：
// 所有滤镜都基于原图算，所以连着点两次"灰度"不会越叠越灰，
// 抠图之后也能一键还原。
class BitmapItem : public QGraphicsPixmapItem
{
public:
    enum { Type = UserType + 2 };
    int type() const override { return Type; }

    explicit BitmapItem(const QImage& img, QGraphicsItem* parent = nullptr);

    const QImage& source() const { return m_source; }    // 原图，只读
    const QImage& current() const { return m_current; }  // 当前显示的那张

    // 用滤镜结果替换「当前显示」，原图不动
    void applyFilter(const QImage& result);

    // 还原成原图
    void resetToSource();

    // 把场景坐标换算成图片像素坐标（魔棒抠图需要点在哪一个像素上）
    QPoint toImagePixel(const QPointF& scenePos) const;

private:
    void refreshPixmap();

    QImage m_source;
    QImage m_current;
};
