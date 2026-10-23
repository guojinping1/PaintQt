#pragma once

#include "Draw.h"

// ============================================================================
//  GameObject —— 画布上所有对象的基类
//
//  按《绘图功能开发与代码架构调整沟通》要求：「继承 GameObject 相关类新建绘图功能类」。
//  这棵继承树是：
//
//      GameObject            抽象基类：有边界、能渲染自己、带 id/可见性/图层
//          └── DrawingObject 绘图功能类：内部持有一个 Shape，实现上面两个纯虚函数
//
//  为什么让 DrawingObject 抱着 Shape 而不是让 Shape 自己继承 GameObject？
//  因为 Shape 是纯数据（值语义，能随便拷贝、SVG 解析直接返回 vector<Shape>），
//  一旦给它加上虚函数，它就变成多态对象，成本上升但收益不大。
//  用 DrawingObject 把「数据」和「对象接口」接起来，两边都不必妥协。
// ============================================================================
namespace draw {

class GameObject
{
public:
    virtual ~GameObject() = default;

    // ---- 子类必须实现 ----

    // 对象在画布坐标系里占的范围（算重绘区域、算导入缩放都要用）
    virtual Bounds bounds() const = 0;

    // 把自己画出来
    virtual void render(Renderer& r) const = 0;

    // ---- 可选覆盖 ----

    // 类名，调试和日志用
    virtual const char* className() const { return "GameObject"; }

    // ---- 通用属性（所有对象都有）----

    int  id() const { return m_id; }
    void setId(int v) { m_id = v; }

    bool visible() const { return m_visible; }
    void setVisible(bool v) { m_visible = v; }

    // 图层：数值大的画在上面
    int  layer() const { return m_layer; }
    void setLayer(int v) { m_layer = v; }

protected:
    GameObject() = default;

private:
    int  m_id = 0;
    bool m_visible = true;
    int  m_layer = 0;
};

} // namespace draw
