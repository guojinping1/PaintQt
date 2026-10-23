#pragma once

#include "GameObject.h"

namespace draw {

// ============================================================================
//  DrawingObject —— 绘图功能类
//
//  这就是文档要求的「继承 GameObject 相关类新建绘图功能类」：
//      GameObject  →  DrawingObject
//
//  职责：把一个值语义的 Shape 包装成一个能进 GameObject 继承树的对象。
//  所有和"怎么画"相关的逻辑仍然在 Draw.cpp 的 renderShape / boundsOf 里，
//  这里只做转发 —— 保证这层没有重复实现。
// ============================================================================
class DrawingObject : public GameObject
{
public:
    explicit DrawingObject(const Shape& s = Shape{}) : m_shape(s) {}

    // 形状数据（可读可写）
    const Shape& shape() const { return m_shape; }
    Shape&       shape()       { return m_shape; }
    void setShape(const Shape& s) { m_shape = s; }

    // ---- GameObject 接口 ----
    Bounds bounds() const override { return boundsOf(m_shape); }
    void   render(Renderer& r) const override { renderShape(m_shape, r); }
    const char* className() const override { return "DrawingObject"; }

    // 便捷转发，上层不用每次写 .shape().type
    ShapeType type() const { return m_shape.type; }
    void      setType(ShapeType t) { m_shape.type = t; }
    unsigned  color() const { return m_shape.color; }
    void      setColor(unsigned c) { m_shape.color = c; }

private:
    Shape m_shape;
};

} // namespace draw
