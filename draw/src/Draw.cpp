#include "Draw.h"

#include <algorithm>
#include <cmath>

namespace draw {

namespace {

void grow(Bounds& b, int x, int y)
{
    if (!b.valid)
    {
        b.left = b.right = x;
        b.top = b.bottom = y;
        b.valid = true;
        return;
    }

    b.left = std::min(b.left, x);
    b.top = std::min(b.top, y);
    b.right = std::max(b.right, x);
    b.bottom = std::max(b.bottom, y);
}

// 贝塞尔至少要 4 个控制点才有意义
bool bezierReady(const Shape& s)
{
    return s.type == ShapeType::Bezier && s.pts.size() >= 4;
}

} // namespace

// 三次贝塞尔：pts 依次是 起点、控制点1、控制点2、终点
// segments 传 0 表示按曲线长度自动决定段数
std::vector<Point> flattenBezier(const std::vector<Point>& pts, int segments)
{
    std::vector<Point> out;
    if (pts.size() < 4)
        return pts;

    const Point& p0 = pts[0];
    const Point& p1 = pts[1];
    const Point& p2 = pts[2];
    const Point& p3 = pts[3];

    if (segments <= 0)
    {
        const double d = std::hypot(double(p1.x - p0.x), double(p1.y - p0.y))
                       + std::hypot(double(p2.x - p1.x), double(p2.y - p1.y))
                       + std::hypot(double(p3.x - p2.x), double(p3.y - p2.y));
        segments = std::clamp(int(d / 3.0) + 2, 8, 96);
    }

    out.reserve(size_t(segments) + 1);
    for (int i = 0; i <= segments; ++i)
    {
        const double t = double(i) / segments;
        const double u = 1.0 - t;

        const double x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
        const double y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;

        out.push_back({ int(std::lround(x)), int(std::lround(y)) });
    }

    return out;
}

void renderShape(const Shape& s, Renderer& r)
{
    // 拖拽方向不定，取一下上下左右
    const int l = std::min(s.x1, s.x2);
    const int t = std::min(s.y1, s.y2);
    const int rr = std::max(s.x1, s.x2);
    const int b = std::max(s.y1, s.y2);

    switch (s.type)
    {
    case ShapeType::Line:
        r.drawLine(s.x1, s.y1, s.x2, s.y2, s.color, s.width);
        break;

    case ShapeType::Rect:
        r.drawRect(l, t, rr, b, s.color, s.width);
        break;

    case ShapeType::Ellipse:
        r.drawEllipse(l, t, rr, b, s.color, s.width);
        break;

    case ShapeType::Triangle:
    {
        std::vector<Point> p = { { (l + rr) / 2, t }, { rr, b }, { l, b } };
        r.drawPolygon(p, s.color, s.width);
        break;
    }

    case ShapeType::Bezier:
        if (bezierReady(s))
            r.drawBezier(s.pts, s.color, s.width);
        break;

    case ShapeType::Stroke:
        for (size_t i = 1; i < s.pts.size(); ++i)
            r.drawLine(s.pts[i - 1].x, s.pts[i - 1].y, s.pts[i].x, s.pts[i].y, s.color, s.width);

        // 线比较粗时，拐弯处会断开，补圆点
        for (const Point& q : s.pts)
            r.fillCircle(q.x, q.y, s.width / 2, s.color);
        break;

    case ShapeType::Polyline:
    {
        for (size_t i = 1; i < s.pts.size(); ++i)
            r.drawLine(s.pts[i - 1].x, s.pts[i - 1].y, s.pts[i].x, s.pts[i].y, s.color, s.width);

        if (s.closed && s.pts.size() > 2)
        {
            const Point& a = s.pts.back();
            const Point& z = s.pts.front();
            r.drawLine(a.x, a.y, z.x, z.y, s.color, s.width);
        }
        break;
    }
    }
}

// ---------------- 节点编辑 ----------------

std::vector<Point> nodesOf(const Shape& s)
{
    switch (s.type)
    {
    case ShapeType::Line:
        return { { s.x1, s.y1 }, { s.x2, s.y2 } };

    // 这三种都由包围盒决定，给两个对角节点
    case ShapeType::Rect:
    case ShapeType::Ellipse:
    case ShapeType::Triangle:
        return { { s.x1, s.y1 }, { s.x2, s.y2 } };

    case ShapeType::Bezier:
        return bezierReady(s) ? s.pts : std::vector<Point>();

    case ShapeType::Polyline:
        return s.pts;

    // 自由笔迹点太多，不给节点，整条拖动就行
    case ShapeType::Stroke:
    default:
        return std::vector<Point>();
    }
}

bool moveNode(Shape& s, int index, int x, int y)
{
    if (index < 0)
        return false;

    switch (s.type)
    {
    case ShapeType::Line:
    case ShapeType::Rect:
    case ShapeType::Ellipse:
    case ShapeType::Triangle:
        if (index == 0) { s.x1 = x; s.y1 = y; return true; }
        if (index == 1) { s.x2 = x; s.y2 = y; return true; }
        return false;

    case ShapeType::Bezier:
    case ShapeType::Polyline:
        if (size_t(index) >= s.pts.size())
            return false;
        s.pts[size_t(index)] = { x, y };
        return true;

    default:
        return false;
    }
}

// ---------------- 包围盒 ----------------

Bounds boundsOf(const Shape& s)
{
    Bounds b;

    if (!s.pts.empty())
    {
        // 有轨迹点的（贝塞尔、自由笔迹、导入的多段线）只按点算，
        // 对它们来说 x1/y1/x2/y2 没有意义，算进去会把包围盒拉大
        for (const Point& q : s.pts)
            grow(b, q.x, q.y);
    }
    else
    {
        grow(b, s.x1, s.y1);
        grow(b, s.x2, s.y2);
    }

    return b;
}

Bounds boundsOf(const std::vector<Shape>& shapes)
{
    Bounds b;

    for (const Shape& s : shapes)
    {
        const Bounds one = boundsOf(s);
        if (!one.valid)
            continue;

        grow(b, one.left, one.top);
        grow(b, one.right, one.bottom);
    }

    return b;
}

// ---------------- 文档 ----------------

void Document::add(const std::vector<Shape>& v)
{
    m_shapes.insert(m_shapes.end(), v.begin(), v.end());
}

bool Document::undo()
{
    if (m_shapes.empty())
        return false;

    m_shapes.pop_back();
    return true;
}

void Document::render(Renderer& r) const
{
    for (const Shape& s : m_shapes)
        renderShape(s, r);
}

} // namespace draw
