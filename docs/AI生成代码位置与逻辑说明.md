# AI 生成代码的位置与逻辑说明

> 对应《绘图功能开发与代码架构调整沟通》中「**需明确 AI 生成代码的位置与逻辑，可修改**」这一条。

## 〇、代码来源声明

本次改造的代码**全部由 AI（DeepSeek Harness）按上述文档的要求生成**。本文的目的不是免责，而是让审查者能：

1. 快速定位每一部分代码在哪
2. 看懂它为什么这么写
3. 知道要改的话改哪里

每一节都尽量给出「**如果你要改成别的做法，动哪个文件**」。

---

## 一、工程分层

```
PaintQt/
├── CMakeLists.txt          顶层：找 Qt、加四个子目录
├── CMakePresets.json       VS Code / 命令行的构建预设（MinGW + Ninja）
├── .vscode/settings.json   C/C++ 和 CMake Tools 插件配置
│
├── draw/                   【静态库①】纯 C++ 图形数据，不依赖 Qt
│   ├── include/Draw.h
│   └── src/Draw.cpp
│
├── qtcanvas/               【静态库②】Qt 画布封装
│   ├── include/QtRenderer.h
│   ├── include/ShapeItem.h        ← 继承 QGraphicsItem 的自定义类
│   ├── include/CanvasScene.h
│   ├── include/CanvasView.h
│   └── src/*.cpp
│
├── svgio/                  【静态库③】SVG 解析
│   ├── include/SvgImport.h
│   └── src/SvgImport.cpp
│
└── APP/                    【主程序】左右分栏界面
    ├── main.cpp
    └── icons/*.svg         左侧功能区的 12 个图标
```

**依赖方向（单向，不循环）：**

```
APP ──► qtcanvas ──► draw          (qtcanvas 依赖 draw 的图形数据)
  ├───► svgio   ──► draw          (svgio 解析出的也是 draw::Shape)
  └───► Qt6::Widgets / Qt6::Svg
```

**这样分层解决了文档里的两条要求：**

| 文档要求 | 对应实现 |
| --- | --- |
| 「画布相关功能封装为独立类」 | `qtcanvas` 静态库 |
| 「独立类导出为静态库，由外层 mainwindow 调用」 | `APP/main.cpp` 里 `MainWindow` 只负责搭界面，画布逻辑全在库里 |

---

## 二、逐文件说明

### 2.1 `draw/` —— 图形数据层（不依赖 Qt）

| 位置 | 内容 | 逻辑 |
| --- | --- | --- |
| `Draw.h` | `ShapeType` 枚举 | 7 种图形：直线/矩形/椭圆/三角形/贝塞尔/笔迹/多段线 |
| | `Shape` 结构 | 图形数据本体，纯 POD，没有 Qt 类型 |
| | `Renderer` 抽象接口 | **关键设计**：库只声明「要画什么」，具体怎么画由上层实现 |
| | `nodesOf` / `moveNode` | 节点编辑的读写接口，上层不用关心各图形类型的坐标差异 |
| | `Document` | 文档容器，管增/撤销/清空 |
| `Draw.cpp` | `renderShape` | 一个 `switch (s.type)`，把图形翻译成 `Renderer` 调用 |
| | `flattenBezier` | 贝塞尔离散成折线（给不方便画曲线的渲染器用） |
| | `boundsOf` | 包围盒，算导入缩放和 `QGraphicsItem::boundingRect` 要用 |

**为什么这层不依赖 Qt？** 文档要求"封装为独立类"，但如果绘图类里直接写 `QPainter`，将来换个界面框架（比如给 C# 上位机做 MFC 版）整个库得重写。现在 `draw` 只认自己的 `Renderer` 接口，Qt 只是它的一个实现。

> **要改**：加新图形类型 → 改 `Draw.h` 的 `ShapeType` + `Draw.cpp` 的 `renderShape` / `nodesOf` / `moveNode` 三个 `switch`。

### 2.2 `qtcanvas/` —— Qt 画布层

| 位置 | 内容 | 逻辑 |
| --- | --- | --- |
| `QtRenderer.h` | `QtRenderer : draw::Renderer` | 把 `Renderer` 的每个方法翻译成 `QPainter` 调用。**这是 draw 库和 Qt 之间唯一的桥** |
| `ShapeItem.h/.cpp` | **`ShapeItem : QGraphicsItem`** | 一个图形 = 一个图元。负责显示、选中、节点手柄、拖拽交互 |
| `CanvasScene.h/.cpp` | `CanvasScene : QGraphicsScene` | 管当前工具、鼠标画图流程、撤销/清空/SVG 导入 |
| `CanvasView.h/.cpp` | `CanvasView : QGraphicsView` | 画白纸+网格，Ctrl+滚轮缩放 |

### 2.3 `svgio/` —— SVG 解析层

| 位置 | 内容 |
| --- | --- |
| `SvgImport.h` | `loadFile` / `loadData`，返回 `Result{shapes, width, height, skipped, arcs}` |
| `SvgImport.cpp` | 用 `QXmlStreamReader` 手写解析：支持 `rect/circle/ellipse/line/polygon/polyline/path`，`path` 支持 `M L H V C S Q T Z`（`A` 弧简化为直线并计数），支持 `transform`（translate/scale/rotate/matrix/skew）和 `style` 属性 |

### 2.4 `APP/main.cpp` —— 界面层

| 内容 | 逻辑 |
| --- | --- |
| `MainWindow` | 只有界面骨架：`QSplitter` 左右分栏 + 左侧功能区 + 状态栏 |
| `kStyleSheet` | 界面样式集中在一个字符串里，改配色只动这里 |
| `buildSidePanel()` | 全代码搭建左侧面板，**工程里没有任何 `.ui` 文件** |
| `iconOf()` | 从 Qt 资源 `:/icons/xxx.svg` 取图标 |

---

## 三、关键实现逻辑（重点看这几段）

### 3.1 节点拖拽编辑怎么实现的

这是文档的核心要求，实现在 **`qtcanvas/src/ShapeItem.cpp`**。

**第一步：算出手柄在哪。** 各图形类型的"可拖节点"不一样，这个差异被 `draw::nodesOf` 挡掉了：

```cpp
// ShapeItem.cpp
QVector<QPointF> ShapeItem::handles() const
{
    QVector<QPointF> out;
    for (const draw::Point& q : draw::nodesOf(m_shape))
        out << QPointF(q.x, q.y);
    return out;
}
```

各类型给什么节点（见 `Draw.cpp` 的 `nodesOf`）：

| 图形 | 节点 |
| --- | --- |
| 直线 | 两个端点 |
| 矩形 / 椭圆 / 三角形 | 包围盒的两个对角 |
| 贝塞尔曲线 | **4 个控制点** |
| 多段线（SVG 导入的） | 全部顶点 |
| 自由笔迹 | 不给（点太多，整条拖动） |

**第二步：按下时判断点在不在手柄上。** 这是"拖节点"和"整体平移"的分岔口：

```cpp
void ShapeItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (isSelected())
    {
        m_dragHandle = handleAt(e->pos());   // 命中哪个手柄
        if (m_dragHandle >= 0) { e->accept(); return; }   // 拖节点
    }
    QGraphicsItem::mousePressEvent(e);       // 否则走基类：整体拖动 / 选中
}
```

**第三步：移动时写回数据。** 这里有个 **Qt 必须遵守的规矩**：

```cpp
void ShapeItem::setHandlePos(int index, const QPointF& p)
{
    prepareGeometryChange();                 // ← 必须在改坐标之前调用！
    if (draw::moveNode(m_shape, index, ...))
        update();
}
```

> ⚠️ `prepareGeometryChange()` 是 `QGraphicsItem` 的强制要求：图元尺寸变了必须提前通知场景，否则 Qt 的 BSP 索引会失效，出现**残影、点选错位**。这是自己写 `QGraphicsItem` 最容易踩的坑。

**第四步：为什么 `shape()` 要改名 `drawShape()`。** `QGraphicsItem` 已经有一个 `shape()` 返回 `QPainterPath`（点选路径）。我一开始的访问器也叫 `shape()`，编译直接报重载冲突。**这是真实踩到的错误，不是预防性设计。**

**第五步：贝塞尔的控制点在曲线外，点不到怎么办。** `QGraphicsItem::shape()` 决定哪些区域能接收鼠标。曲线路径本身不覆盖控制点，所以选中时要把手柄也加进点选路径：

```cpp
QPainterPath ShapeItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(std::max<qreal>(m_shape.width, 10.0));   // 细线也好点中
    QPainterPath hit = stroker.createStroke(pathOf(m_shape));

    if (isSelected())
        for (const QPointF& h : handles())
            hit.addEllipse(h, kHitRadius, kHitRadius);        // ← 关键

    return hit;
}
```

> **要改**：想加"旋转手柄""中点插入节点"之类的 → 改 `ShapeItem` 的 `handles()` / `handleAt()` / `setHandlePos()`，`draw` 库那边对应加 `moveNode` 的 case。

### 3.2 贝塞尔曲线的生成与编辑

**拖拽生成**（`CanvasScene::updateShape`）：按下点是起点，当前鼠标是终点，两个控制点按 1/3、2/3 横向位置贴在两端的水平高度上，出来是个 S 形：

```cpp
const int x0 = s.pts[0].x, y0 = s.pts[0].y;
s.pts[3] = { x, y };                          // 终点
s.pts[1] = { x0 + (x - x0) / 3, y0 };         // 控制点1
s.pts[2] = { x0 + (x - x0) * 2 / 3, y };      // 控制点2
```

**画出来**（`QtRenderer::drawBezier`）：用 `QPainterPath::cubicTo`，Qt 自己做抗锯齿曲线渲染，不做离散。

**为什么还留着 `flattenBezier`？** `draw` 库要能对接不支持曲线的渲染器（比如 GDI 纯 `LineTo`）。Qt 这条路径用不到它，但接口留着了。

### 3.3 橡皮擦

没有做"擦除像素"，而是**用背景色画一条粗笔迹**：

```cpp
// CanvasScene::beginShape
s.color = (m_tool == Tool::Eraser) ? 0xFFFFFF : m_color;   // 白色
s.width = (m_tool == Tool::Eraser) ? 22 : m_penWidth;      // 粗
s.type  = draw::ShapeType::Stroke;
```

好处是橡皮擦擦除的痕迹**也是一个图形对象**，能被撤销、能跟着一起缩放。
代价是它只能擦白底，擦不掉背景图片——当前没有背景图，够用。

### 3.4 SVG 导入解析流程

`svgio/src/SvgImport.cpp`，手写递归下降解析：

1. `QXmlStreamReader` 逐节点读
2. 维护两个**栈式状态**：样式（`stroke` / `stroke-width`，`style=""` 优先级高于属性）和变换矩阵（自己实现的 2×2 仿射 `Xform`，不引 QtGui）
3. 遇到 `<g>` 往下递归，遇到 `<defs>/<style>/<title>` 整棵子树跳过
4. 每类元素转成 `draw::Shape`：
   - `rect` 在**轴对齐**变换下转成矩形，**被旋转/斜切**时转成闭合多段线（4 个角点变换后不再是矩形）
   - `ellipse/circle` 同理：轴对齐转椭圆，否则用 64 段折线近似
   - `path` 的 `d` 属性自己写词法+语法分析，三次/二次贝塞尔按控制多边形长度自适应离散（8~96 段）
   - `A` 圆弧命令**简化成直线**，并在 `Result.arcs` 里计数，界面上会提示"N 段圆弧已简化为直线"——不做假装支持
5. 文件名 → `viewBox` 缩放 → 交给 `CanvasScene::addShapes` 按画布大小居中缩放进场景

> **要改**：想支持 `A` 圆弧 → 在 `parsePathData` 的 `C == 'A'` 分支里做"端点参数化 → 中心参数化 → 转三次贝塞尔"。想支持 `<use>` → 在 `parseNode` 里把 `defs` 里的 `id` 存下来再引用。

### 3.5 左右分栏与图标

`APP/main.cpp` 的 `buildSidePanel()`：`QVBoxLayout` 竖排「标题 / 绘图工具 / 颜色 / 操作 / 提示」，工具按钮用 `QGridLayout` 排 3 列。

图标是 12 个 24×24 的线性 SVG（`APP/icons/`），走 Qt 资源系统编进 exe：

```cmake
qt_add_resources(APP "app_icons"
    PREFIX "/icons"
    BASE   "icons"
    FILES icons/select.svg ... )
```

> **要换成阿里 iconfont 的图标**：把 `APP/icons/` 里对应的 `.svg` 替换掉即可，文件名不用变。如果用字体图标（`.ttf`），需要改 `iconOf()` 用 `QFont` + 码点绘制。

---

## 四、改造中踩到的两个 CMake 坑（已修，记录备查）

### 坑 1：AUTOMOC 不会扫描 `include/` 目录下的头文件

`qtcanvas` 的头文件放在 `include/` 而不是和 `.cpp` 同目录，AUTOMOC 默认**找不到 `Q_OBJECT`**，生成的 `mocs_compilation.cpp` 是空的，链接时报：

```
undefined reference to `CanvasScene::staticMetaObject'
undefined reference to `vtable for CanvasScene'
```

**修法**：把头文件也列进目标的源文件列表（见 `qtcanvas/CMakeLists.txt`）。

### 坑 2：`qt_standard_project_setup()` 不会打开 AUTORCC

原来把 `resources.qrc` 列进 `qt_add_executable` 就算了，结果 `.qrc` 被**静默忽略**——`build.ninja` 里根本没有 rcc 规则，exe 里也没有资源数据，表现是**所有图标按钮全是空白**（没有图标也没有文字，渲染成透明）。

**修法**：改用 `qt_add_resources()`（见 `APP/CMakeLists.txt`）。

---

## 五、建议重点审查的位置

按"改起来影响最大"排序：

| 位置 | 为什么值得看 | 可能的改法 |
| --- | --- | --- |
| `Draw.h` 的 `Renderer` 接口 | 整个分层的关键，改它要动 `QtRenderer` | 加 `drawArc` / `fillPolygon` 等 |
| `Draw.cpp` 的 `nodesOf` / `moveNode` | 节点编辑的数据源头 | 三角形现在是 2 个包围盒节点，可以改成 3 个顶点节点（但要扩 `Shape` 的字段） |
| `ShapeItem::shape()` | 决定哪些地方能点中 | 调 `kHitRadius` / 描边宽度可以改善手感 |
| `CanvasScene::beginShape/updateShape/finishShape` | 三种鼠标事件的画图流程 | 加"按住 Shift 画正方形/正圆"等约束 |
| `SvgImport.cpp` 的 `parsePathData` | SVG 解析最复杂的一段 | 上面的"要改"里列了 |
| `kStyleSheet` | 所有配色圆角 | 换主题 |

**我明确没做、也没假装做的**（避免审查时以为是 bug）：

- `path` 的 `A` 圆弧命令：简化为直线，且**界面上会提示有几段被简化了**
- 渐变（`linearGradient` / `radialGradient`）：整棵子树跳过，`Result.skipped` 计数
- 填充（`fill`）：只画轮廓。`fill` 只在"没有 `stroke`"时被借用来当线色
- 文字（`<text>`）：跳过
- 自由笔迹不提供节点（点太多）

---

## 六、运行测试记录

全部在 MinGW GCC 13.1.0 + Qt 6.10.2 + CMake 3.30.5 + Ninja 1.12.1 下实测：

| 测试项 | 结果 | 截图 |
| --- | --- | --- |
| 编译 | 三个静态库 + APP.exe 全部通过，0 错误 | — |
| 主界面 | 左右分栏、12 个图标按钮正常显示 | `docs/01-主界面.png` |
| 六种图形绘制 | 直线/矩形/椭圆/三角形/贝塞尔/铅笔全部画出 | `docs/02-绘制六种图形.png` |
| 选中显示节点 | 蓝色虚线框 + 4 个白色手柄 + 控制多边形 | `docs/03-选中图形显示节点.png` |
| **节点拖拽编辑** | 拖动控制点，曲线形状实时改变 | `docs/04-拖拽节点编辑形状.png` |
| SVG 导入 | 8 类元素 + `style` 属性 + `transform` 分组全部正确解析渲染 | `docs/05-SVG导入.png` |
| 撤销 / 清空 / 保存 PNG | 已实现（未单独截图） | — |

**测试方式**：用 `PostMessage` 向窗口发送真实的鼠标按下/移动/抬起消息，程序走的是和真人操作完全一样的代码路径；截图用 `CopyFromScreen` 抓客户区。

---

## 七、怎么跑起来

```powershell
# 命令行构建（VS Code 里按 F7 也行）
$env:PATH = "D:\qt\Tools\mingw1310_64\bin;D:\qt\Tools\Ninja;D:\qt\Tools\CMake_64\bin;" + $env:PATH
cmake --preset mingw-debug
cmake --build --preset mingw-debug

# 运行（要把 Qt 的 DLL 目录加进 PATH）
$env:PATH = "D:\qt\6.10.2\mingw_64\bin;" + $env:PATH
.\build\mingw-debug\bin\APP.exe

# 命令行直接打开一个 SVG（也方便测试）
.\build\mingw-debug\bin\APP.exe .\samples\shapes.svg
```
