#pragma once

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygon>

#include <algorithm>

#include "Draw.h"

// 把 draw 库的渲染接口用 QPainter 实现。
// draw 库本身不认识 Qt，靠这个类把"图形数据"翻译成 Qt 的绘制调用。
class QtRenderer : public draw::Renderer
{
public:
    explicit QtRenderer(QPainter& p) : m_p(p) {}

    void drawLine(int x1, int y1, int x2, int y2, unsigned c, int w) override
    {
        m_p.setPen(makePen(c, w));
        m_p.setBrush(Qt::NoBrush);
        m_p.drawLine(x1, y1, x2, y2);
    }

    void drawRect(int l, int t, int r, int b, unsigned c, int w) override
    {
        m_p.setPen(makePen(c, w));
        m_p.setBrush(Qt::NoBrush);
        m_p.drawRect(QRect(l, t, r - l, b - t));
    }

    void drawEllipse(int l, int t, int r, int b, unsigned c, int w) override
    {
        m_p.setPen(makePen(c, w));
        m_p.setBrush(Qt::NoBrush);
        m_p.drawEllipse(QRect(l, t, r - l, b - t));
    }

    void drawPolygon(const std::vector<draw::Point>& pts, unsigned c, int w) override
    {
        QPolygon poly;
        for (const draw::Point& q : pts)
            poly << QPoint(q.x, q.y);

        m_p.setPen(makePen(c, w));
        m_p.setBrush(Qt::NoBrush);
        m_p.drawPolygon(poly);
    }

    void drawBezier(const std::vector<draw::Point>& pts, unsigned c, int w) override
    {
        if (pts.size() < 4)
            return;

        QPainterPath path;
        path.moveTo(pts[0].x, pts[0].y);
        path.cubicTo(pts[1].x, pts[1].y, pts[2].x, pts[2].y, pts[3].x, pts[3].y);

        m_p.setPen(makePen(c, w));
        m_p.setBrush(Qt::NoBrush);
        m_p.drawPath(path);
    }

    void fillCircle(int x, int y, int radius, unsigned c) override
    {
        if (radius <= 0)
            return;

        m_p.setPen(Qt::NoPen);
        m_p.setBrush(toColor(c));
        m_p.drawEllipse(QPoint(x, y), radius, radius);
    }

    static QColor toColor(unsigned c)
    {
        return QColor(int((c >> 16) & 0xFF), int((c >> 8) & 0xFF), int(c & 0xFF));
    }

private:
    static QPen makePen(unsigned c, int w)
    {
        QPen p(toColor(c));
        p.setWidthF(std::max(1.0, double(w)));
        p.setCapStyle(Qt::RoundCap);
        p.setJoinStyle(Qt::RoundJoin);
        return p;
    }

    QPainter& m_p;
};
