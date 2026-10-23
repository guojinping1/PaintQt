#pragma once

#include <vector>

// 绘图类库。只负责"图形数据 + 怎么把图形交给渲染器"，
// 不依赖任何界面框架（不认识 Qt，也不认识 Windows GDI）。
// 由上层实现下面的 Renderer 接口来真正画图：
//   - qtcanvas 里用 QPainterRenderer 实现（Qt 版）
// 节点编辑（nodesOf / moveNode）也放在这里，方便换界面框架时复用。
namespace draw {

enum class ShapeType
{
    Line,       // 直线
    Rect,       // 矩形
    Ellipse,    // 椭圆
    Triangle,   // 三角形
    Bezier,     // 三次贝塞尔曲线（控制点存在 pts 里，固定 4 个）
    Stroke,     // 自由笔迹（铅笔、橡皮擦）
    Polyline    // 多段线（SVG 等矢量文件导入后用这个）
};

struct Point
{
    int x = 0;
    int y = 0;
};

struct Shape
{
    ShapeType type = ShapeType::Line;
    int       x1 = 0, y1 = 0;       // 直线/矩形/椭圆/三角形的起止点
    int       x2 = 0, y2 = 0;
    unsigned  color = 0x000000;     // 0xRRGGBB
    int       width = 1;            // 线宽
    bool      closed = false;       // Polyline 是否首尾相连
    std::vector<Point> pts;         // Bezier(4个) / Stroke / Polyline 用
};

// 渲染接口。上层实现它（Qt 就用 QPainter 实现）
class Renderer
{
public:
    virtual ~Renderer() = default;

    virtual void drawLine(int x1, int y1, int x2, int y2, unsigned color, int width) = 0;
    virtual void drawRect(int l, int t, int r, int b, unsigned color, int width) = 0;
    virtual void drawEllipse(int l, int t, int r, int b, unsigned color, int width) = 0;
    virtual void drawPolygon(const std::vector<Point>& pts, unsigned color, int width) = 0;

    // 三次贝塞尔：pts 依次是 起点、控制点1、控制点2、终点
    virtual void drawBezier(const std::vector<Point>& pts, unsigned color, int width) = 0;

    virtual void fillCircle(int x, int y, int radius, unsigned color) = 0;
};

// 把一个图形交给渲染器
void renderShape(const Shape& s, Renderer& r);

// 把贝塞尔曲线离散成折线（渲染器不方便画曲线时用）
std::vector<Point> flattenBezier(const std::vector<Point>& pts, int segments = 32);

// ---------------- 节点编辑 ----------------

// 取图形上可以拖拽的节点。返回的就是节点坐标，
// 下标 i 可以直接传给 moveNode()。
// 自由笔迹点太多，不提供节点（返回空）。
std::vector<Point> nodesOf(const Shape& s);

// 把第 index 个节点挪到 (x, y)。成功返回 true
bool moveNode(Shape& s, int index, int x, int y);

// ---------------- 包围盒 ----------------

struct Bounds
{
    int  left = 0, top = 0, right = 0, bottom = 0;
    bool valid = false;
};

Bounds boundsOf(const std::vector<Shape>& shapes);
Bounds boundsOf(const Shape& s);

// ---------------- 文档 ----------------

// 画布上的全部内容
class Document
{
public:
    void   add(const Shape& s) { m_shapes.push_back(s); }
    void   add(const std::vector<Shape>& v);
    bool   undo();
    void   clear() { m_shapes.clear(); }
    size_t count() const { return m_shapes.size(); }

    const std::vector<Shape>& shapes() const { return m_shapes; }

    void render(Renderer& r) const;

private:
    std::vector<Shape> m_shapes;
};

} // namespace draw
