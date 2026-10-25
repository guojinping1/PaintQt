#include "DxfImport.h"

#include <QFile>
#include <QHash>
#include <QList>

#include <algorithm>
#include <cmath>

namespace dxfio {

namespace {

constexpr double kPi = 3.14159265358979323846;

int iround(double v)
{
    return int(std::lround(v));
}

double deg2rad(double d)
{
    return d * kPi / 180.0;
}

// ---------------------------------------------------------------------------
//  HSV → RGB。自己写是为了让 dxfio 只依赖 Qt6::Core，
//  不引入 QColor（那是 Qt6::Gui 里的，会拖进界面框架依赖）。
// ---------------------------------------------------------------------------
unsigned hsvToRgb(double h, double s, double v)
{
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;

    double r = 0, g = 0, b = 0;

    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }

    const int R = int((r + m) * 255.0 + 0.5);
    const int G = int((g + m) * 255.0 + 0.5);
    const int B = int((b + m) * 255.0 + 0.5);

    return unsigned((R << 16) | (G << 8) | B);
}

// ---------------------------------------------------------------------------
//  ACI 索引色 → 0xRRGGBB
//  DXF 里颜色存的是 AutoCAD Color Index，不是 RGB，得查表换算。
// ---------------------------------------------------------------------------
unsigned aciToRgb(int aci)
{
    // 0~9 是几个最常用的固定色
    static const unsigned basic[10] =
    {
        0x000000,   // 0 = BYBLOCK，当作黑色
        0xFF0000,   // 1 红
        0xFFFF00,   // 2 黄
        0x00FF00,   // 3 绿
        0x00FFFF,   // 4 青
        0x0000FF,   // 5 蓝
        0xFF00FF,   // 6 洋红
        0x000000,   // 7 白/黑 —— 我们的画布是白底，所以要画成黑色
        0x808080,   // 8 深灰
        0xC0C0C0,   // 9 浅灰
    };

    if (aci >= 0 && aci <= 9)
        return basic[aci];

    // 250~255：由深到浅的六档灰
    if (aci >= 250 && aci <= 255)
    {
        const int v = 0x33 + (aci - 250) * (0xFF - 0x33) / 5;
        return unsigned((v << 16) | (v << 8) | v);
    }

    // 10~249：24 个色相 × 10 级明暗。
    // 这是 ACI 调色板的结构：每 10 个一组，色调转 15 度。
    if (aci >= 10 && aci <= 249)
    {
        const int hue = (aci - 10) / 10;     // 0..23
        const int var = (aci - 10) % 10;     // 0..9

        static const int val[10] = { 100, 80, 60, 45, 30, 100, 80, 60, 45, 30 };
        static const int sat[10] = { 100, 100, 100, 100, 100, 50, 50, 50, 50, 50 };

        return hsvToRgb(hue * 15.0, sat[var] / 100.0, val[var] / 100.0);
    }

    return 0x000000;    // 256 = BYLAYER 等，交给调用方处理
}

// ---------------------------------------------------------------------------
//  一个"组码/值"对。DXF 整个文件就是这种东西的序列。
// ---------------------------------------------------------------------------
struct Raw
{
    int     code = 0;
    QString value;
};

// 一个实体：从 "0/类型" 开始，到下一个 "0/" 之前的所有组码
struct Entity
{
    QString          type;
    std::vector<Raw> tags;

    bool contains(int code) const
    {
        for (const Raw& t : tags)
            if (t.code == code) return true;
        return false;
    }
};

QString tagStr(const Entity& e, int code, const QString& def = QString())
{
    for (const Raw& t : e.tags)
        if (t.code == code) return t.value;
    return def;
}

int tagInt(const Entity& e, int code, int def = 0)
{
    for (const Raw& t : e.tags)
        if (t.code == code)
        {
            bool ok = false;
            const int v = t.value.trimmed().toInt(&ok);
            return ok ? v : def;
        }
    return def;
}

double tagDbl(const Entity& e, int code, double def = 0)
{
    for (const Raw& t : e.tags)
        if (t.code == code)
        {
            bool ok = false;
            const double v = t.value.trimmed().toDouble(&ok);
            return ok ? v : def;
        }
    return def;
}

// ---------------------------------------------------------------------------
//  把文件切成组码序列
// ---------------------------------------------------------------------------
bool tokenize(const QByteArray& data, std::vector<Raw>& out, QString& err)
{
    QByteArray text = data;
    text.replace("\r\n", "\n");
    if (!text.contains('\n'))
        text.replace('\r', '\n');       // 兼容只用 \r 换行的老文件

    const QList<QByteArray> lines = text.split('\n');

    out.clear();
    out.reserve(size_t(lines.size() / 2 + 8));

    int i = 0;
    while (i + 1 < lines.size())
    {
        const QByteArray codeLine = lines[i].trimmed();

        if (codeLine.isEmpty())         // 允许空行
        {
            ++i;
            continue;
        }

        bool okNum = false;
        const int code = codeLine.toInt(&okNum);

        if (!okNum)
        {
            err = QStringLiteral("第 %1 行不是合法的组码（应为整数），实际是：%2")
                      .arg(i + 1)
                      .arg(QString::fromLatin1(codeLine.left(32)));
            return false;
        }

        QByteArray valueLine = lines[i + 1];
        if (valueLine.endsWith('\r'))
            valueLine.chop(1);

        // 用 Latin1 解码：DXF 里的文字可能是 GBK，这里只要"能稳定当键用"就行，
        // 真正的显示不依赖它（我们只用图层名去查颜色）。
        out.push_back(Raw{ code, QString::fromLatin1(valueLine) });
        i += 2;
    }

    if (out.empty())
    {
        err = QStringLiteral("文件是空的");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
//  把一个实体换成若干 draw::Shape（坐标还是 DXF 的，Y 轴向上）
// ---------------------------------------------------------------------------

// 取实体的颜色：优先用实体自己的 62，BYLAYER 时查图层表
unsigned colorOf(const Entity& e, const QHash<QString, unsigned>& layerColor)
{
    const int c = tagInt(e, 62, 256);       // 256 = BYLAYER

    if (c == 256 || c == 0)                 // BYLAYER / BYBLOCK
    {
        const auto it = layerColor.find(tagStr(e, 8));
        if (it != layerColor.end())
            return *it;
        return 0x000000;
    }

    if (c < 0)                              // 负数表示图层被关闭，颜色取绝对值
        return aciToRgb(-c);

    return aciToRgb(c);
}

// 多边形闭合标志：组码 70 的 bit0
bool isClosed(const Entity& e)
{
    return (tagInt(e, 70, 0) & 1) != 0;
}

// 读 LWPOLYLINE 的顶点：同一个实体里重复出现 10/20 对
std::vector<draw::Point> lwVertices(const Entity& e, bool flipY)
{
    std::vector<draw::Point> pts;

    double px = 0;
    bool haveX = false;

    for (const Raw& t : e.tags)
    {
        if (t.code == 10)
        {
            px = t.value.trimmed().toDouble();
            haveX = true;
        }
        else if (t.code == 20 && haveX)
        {
            const double py = t.value.trimmed().toDouble();
            pts.push_back(draw::Point{ iround(px), iround(flipY ? -py : py) });
            haveX = false;
        }
    }

    return pts;
}

draw::Shape makePolyline(std::vector<draw::Point> pts, bool closed,
                         unsigned color, int width)
{
    draw::Shape s;
    s.type   = draw::ShapeType::Polyline;
    s.closed = closed;
    s.color  = color;
    s.width  = width;
    s.pts    = std::move(pts);

    if (!s.pts.empty())
    {
        s.x1 = s.pts.front().x;
        s.y1 = s.pts.front().y;
        s.x2 = s.pts.back().x;
        s.y2 = s.pts.back().y;
    }

    return s;
}

} // namespace

// ===========================================================================
//  主解析流程
// ===========================================================================
Result loadData(const QByteArray& data)
{
    Result r;

    std::vector<Raw> tags;
    if (!tokenize(data, tags, r.error))
        return r;

    // ---- 第一遍：按段收集实体，顺便读图层颜色 ----
    std::vector<Entity>           entities;     // ENTITIES 段里的实体
    QHash<QString, std::vector<Entity>> blocks; // BLOCKS 段：块名 → 块内实体
    QHash<QString, unsigned>      layerColor;   // 图层名 → 颜色

    QString section;
    QString curBlock;

    size_t i = 0;
    while (i < tags.size())
    {
        if (tags[i].code != 0)
        {
            ++i;
            continue;
        }

        const QString head = tags[i].value;

        if (head == QStringLiteral("EOF"))
            break;

        if (head == QStringLiteral("SECTION"))
        {
            section = (i + 1 < tags.size() && tags[i + 1].code == 2)
                          ? tags[i + 1].value
                          : QString();
            i += 2;
            continue;
        }

        if (head == QStringLiteral("ENDSEC"))
        {
            section.clear();
            curBlock.clear();
            ++i;
            continue;
        }

        // 读出一个完整实体（到下一个 0 为止）
        Entity e;
        e.type = head;
        ++i;
        while (i < tags.size() && tags[i].code != 0)
        {
            e.tags.push_back(tags[i]);
            ++i;
        }

        if (section == QStringLiteral("ENTITIES"))
        {
            entities.push_back(std::move(e));
        }
        else if (section == QStringLiteral("BLOCKS"))
        {
            if (e.type == QStringLiteral("BLOCK"))
            {
                curBlock = tagStr(e, 2);
                if (curBlock.isEmpty())
                    curBlock = QStringLiteral("__unnamed__");
                blocks[curBlock];       // 建一个空条目
            }
            else if (e.type == QStringLiteral("ENDBLK"))
            {
                curBlock.clear();
            }
            else if (!curBlock.isEmpty() && e.type != QStringLiteral("SEQEND"))
            {
                blocks[curBlock].push_back(std::move(e));
            }
        }
        else if (section == QStringLiteral("TABLES"))
        {
            if (e.type == QStringLiteral("LAYER"))
            {
                const QString name = tagStr(e, 2);
                int c = tagInt(e, 62, 7);
                if (c < 0)
                    c = -c;             // 负数表示图层关闭，颜色仍有效

                if (!name.isEmpty())
                    layerColor[name] = aciToRgb(c);
            }
        }
    }

    // ---- 第二遍：把实体转成图形 ----
    std::vector<draw::Shape> shapes;

    // 块内实体先转一遍（坐标不翻转，保持 DXF 原始方向，方便后面做插入变换）
    QHash<QString, std::vector<draw::Shape>> blockShapes;
    for (auto it = blocks.cbegin(); it != blocks.cend(); ++it)
    {
        std::vector<draw::Shape> inner;

        for (const Entity& be : it.value())
        {
            const unsigned color = colorOf(be, layerColor);

            if (be.type == QStringLiteral("LINE"))
            {
                draw::Shape s;
                s.type  = draw::ShapeType::Line;
                s.x1    = iround(tagDbl(be, 10));
                s.y1    = iround(tagDbl(be, 20));
                s.x2    = iround(tagDbl(be, 11));
                s.y2    = iround(tagDbl(be, 21));
                s.color = color;
                s.width = 1;
                inner.push_back(s);
            }
            else if (be.type == QStringLiteral("LWPOLYLINE"))
            {
                inner.push_back(makePolyline(lwVertices(be, false), isClosed(be), color, 1));
            }
            else if (be.type == QStringLiteral("CIRCLE"))
            {
                const double cx = tagDbl(be, 10);
                const double cy = tagDbl(be, 20);
                const double rr = tagDbl(be, 40);

                draw::Shape s;
                s.type  = draw::ShapeType::Ellipse;
                s.x1    = iround(cx - rr);
                s.y1    = iround(cy - rr);
                s.x2    = iround(cx + rr);
                s.y2    = iround(cy + rr);
                s.color = color;
                s.width = 1;
                inner.push_back(s);
            }
            else
            {
                // 块内的 ARC/ELLIPSE/嵌套 INSERT 不展开，避免递归失控
                ++r.skipped;
            }
        }

        blockShapes[it.key()] = std::move(inner);
    }

    // ---- 顶层实体 ----
    for (size_t k = 0; k < entities.size(); ++k)
    {
        const Entity& e = entities[k];

        // ---- 直线 ----
        if (e.type == QStringLiteral("LINE"))
        {
            draw::Shape s;
            s.type  = draw::ShapeType::Line;
            s.x1    = iround(tagDbl(e, 10));
            s.y1    = iround(tagDbl(e, 20));
            s.x2    = iround(tagDbl(e, 11));
            s.y2    = iround(tagDbl(e, 21));
            s.color = colorOf(e, layerColor);
            s.width = 1;
            shapes.push_back(s);
        }
        // ---- 轻量多段线 ----
        else if (e.type == QStringLiteral("LWPOLYLINE"))
        {
            auto pts = lwVertices(e, false);
            if (pts.size() >= 2)
                shapes.push_back(makePolyline(std::move(pts), isClosed(e),
                                              colorOf(e, layerColor), 1));
            else
                ++r.skipped;
        }
        // ---- 老式多段线：顶点是后面独立的 VERTEX 实体 ----
        else if (e.type == QStringLiteral("POLYLINE"))
        {
            std::vector<draw::Point> pts;

            size_t j = k + 1;
            for (; j < entities.size() && entities[j].type == QStringLiteral("VERTEX"); ++j)
            {
                const double vx = tagDbl(entities[j], 10);
                const double vy = tagDbl(entities[j], 20);
                pts.push_back(draw::Point{ iround(vx), iround(vy) });
            }

            // 跳过 VERTEX 和结尾的 SEQEND
            if (j < entities.size() && entities[j].type == QStringLiteral("SEQEND"))
                k = j;
            else if (j > k + 1)
                k = j - 1;

            if (pts.size() >= 2)
                shapes.push_back(makePolyline(std::move(pts), isClosed(e),
                                              colorOf(e, layerColor), 1));
            else
                ++r.skipped;
        }
        // ---- 圆 ----
        else if (e.type == QStringLiteral("CIRCLE"))
        {
            const double cx = tagDbl(e, 10);
            const double cy = tagDbl(e, 20);
            const double rr = tagDbl(e, 40);

            if (rr <= 0)
            {
                ++r.skipped;
                continue;
            }

            draw::Shape s;
            s.type  = draw::ShapeType::Ellipse;
            s.x1    = iround(cx - rr);
            s.y1    = iround(cy - rr);
            s.x2    = iround(cx + rr);
            s.y2    = iround(cy + rr);
            s.color = colorOf(e, layerColor);
            s.width = 1;
            shapes.push_back(s);
        }
        // ---- 圆弧：按角度采样成折线 ----
        else if (e.type == QStringLiteral("ARC"))
        {
            const double cx = tagDbl(e, 10);
            const double cy = tagDbl(e, 20);
            const double rr = tagDbl(e, 40);
            double a0 = tagDbl(e, 50, 0.0);
            double a1 = tagDbl(e, 51, 360.0);

            if (rr <= 0)
            {
                ++r.skipped;
                continue;
            }

            if (a1 <= a0)
                a1 += 360.0;        // 跨越 0 度的圆弧

            const int steps = std::max(6, int((a1 - a0) / 4.0));   // 每 4 度一段

            std::vector<draw::Point> pts;
            pts.reserve(size_t(steps + 1));
            for (int s = 0; s <= steps; ++s)
            {
                const double a = deg2rad(a0 + (a1 - a0) * s / steps);
                pts.push_back(draw::Point{ iround(cx + rr * std::cos(a)),
                                           iround(cy + rr * std::sin(a)) });
            }

            shapes.push_back(makePolyline(std::move(pts), false,
                                          colorOf(e, layerColor), 1));
            ++r.curves;
        }
        // ---- 椭圆 / 椭圆弧 ----
        else if (e.type == QStringLiteral("ELLIPSE"))
        {
            const double cx = tagDbl(e, 10);
            const double cy = tagDbl(e, 20);
            const double mx = tagDbl(e, 11);        // 长轴端点（相对圆心）
            const double my = tagDbl(e, 21);
            double ratio = tagDbl(e, 40, 1.0);
            const double t0 = tagDbl(e, 41, 0.0);
            const double t1 = tagDbl(e, 42, 2.0 * kPi);

            if (ratio <= 0)
                ratio = 1.0;

            // 短轴向量 = 长轴旋转 90 度，再乘长短轴比
            const double bx = -my * ratio;
            const double by =  mx * ratio;

            const int steps = std::max(12, int(std::fabs(t1 - t0) / 0.15));

            std::vector<draw::Point> pts;
            pts.reserve(size_t(steps + 1));
            for (int s = 0; s <= steps; ++s)
            {
                const double t = t0 + (t1 - t0) * s / steps;
                pts.push_back(draw::Point{
                    iround(cx + mx * std::cos(t) + bx * std::sin(t)),
                    iround(cy + my * std::cos(t) + by * std::sin(t)) });
            }

            shapes.push_back(makePolyline(std::move(pts), false,
                                          colorOf(e, layerColor), 1));
            ++r.curves;
        }
        // ---- 图块引用：把块内容平移+缩放后铺进来 ----
        else if (e.type == QStringLiteral("INSERT"))
        {
            const QString name = tagStr(e, 2);
            const auto it = blockShapes.find(name);

            if (it == blockShapes.end())
            {
                ++r.skipped;
                continue;
            }

            const double ox = tagDbl(e, 10);
            const double oy = tagDbl(e, 20);
            double sx = tagDbl(e, 41, 1.0);
            double sy = tagDbl(e, 42, 1.0);

            if (sx == 0) sx = 1;
            if (sy == 0) sy = 1;

            for (const draw::Shape& bs : it.value())
            {
                draw::Shape s = bs;

                // 变换：点先缩放再平移
                auto tx = [&](int& x, int& y)
                {
                    x = iround(ox + x * sx);
                    y = iround(oy + y * sy);
                };

                tx(s.x1, s.y1);
                tx(s.x2, s.y2);
                for (draw::Point& p : s.pts)
                    tx(p.x, p.y);

                // 负缩放会让矩形/椭圆反个方向，归一化一下
                if (s.x1 > s.x2) std::swap(s.x1, s.x2);
                if (s.y1 > s.y2) std::swap(s.y1, s.y2);

                shapes.push_back(s);
            }

            ++r.blocks;
        }
        // ---- 顶点 / 序列结束：上面已经消费掉了，这里跳过 ----
        else if (e.type == QStringLiteral("VERTEX") || e.type == QStringLiteral("SEQEND"))
        {
            // 不计数
        }
        else
        {
            // TEXT / MTEXT / DIMENSION / HATCH / SOLID / POINT / SPLINE / 3DFACE ...
            ++r.skipped;
        }
    }

    if (shapes.empty())
    {
        r.ok = false;
        r.error = r.skipped > 0
                      ? QStringLiteral("这个 DXF 里没有可导入的线条（全部 %1 个元素都不支持）")
                            .arg(r.skipped)
                      : QStringLiteral("这个 DXF 里没有任何实体");
        return r;
    }

    // ---- 第三遍：Y 轴翻转 + 归一化 ----
    //
    // DXF 的 Y 轴朝上，屏幕的 Y 轴朝下，所以要翻一次。
    // 另外 CAD 图元的坐标可能只有 0.001 单位、也可能是 100000 单位，
    // draw::Shape 用的是整数像素，先统一缩放到 1000 左右再交给
    // addShapes(fitToView) 去适配画布，才不会因为取整丢掉细节。
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;

    for (draw::Shape& s : shapes)
    {
        s.y1 = -s.y1;
        s.y2 = -s.y2;

        for (draw::Point& p : s.pts)
            p.y = -p.y;

        // 矩形类图形翻转后上下颠倒，归一化回"左上/右下"
        if (s.type == draw::ShapeType::Rect ||
            s.type == draw::ShapeType::Ellipse ||
            s.type == draw::ShapeType::Triangle)
        {
            if (s.x1 > s.x2) std::swap(s.x1, s.x2);
            if (s.y1 > s.y2) std::swap(s.y1, s.y2);
        }

        const draw::Bounds b = draw::boundsOf(s);
        if (b.valid)
        {
            minX = std::min(minX, double(b.left));
            minY = std::min(minY, double(b.top));
            maxX = std::max(maxX, double(b.right));
            maxY = std::max(maxY, double(b.bottom));
        }
    }

    if (maxX > minX || maxY > minY)
    {
        const double extent = std::max(maxX - minX, maxY - minY);
        const double k = (extent > 0) ? (1000.0 / extent) : 1.0;

        auto apply = [&](int& x, int& y)
        {
            x = iround((x - minX) * k);
            y = iround((y - minY) * k);
        };

        for (draw::Shape& s : shapes)
        {
            apply(s.x1, s.y1);
            apply(s.x2, s.y2);
            for (draw::Point& p : s.pts)
                apply(p.x, p.y);
        }
    }

    r.ok     = true;
    r.shapes = std::move(shapes);
    return r;
}

Result loadFile(const QString& fileName)
{
    Result r;

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        r.error = QStringLiteral("打不开文件：%1").arg(fileName);
        return r;
    }

    const QByteArray data = f.readAll();
    f.close();

    if (data.isEmpty())
    {
        r.error = QStringLiteral("文件是空的：%1").arg(fileName);
        return r;
    }

    // 快速识别：DWG 是二进制格式，开头是 "AC10xx"
    if (data.startsWith("AC10") && data.size() > 8 &&
        data.at(6) != '\n' && data.at(6) != '\r')
    {
        r.error = QStringLiteral(
            "这是 DWG 二进制文件，不是 DXF。\n"
            "请在 CAD 里用「另存为」导出 DXF（ASCII）格式再导入。");
        return r;
    }

    return loadData(data);
}

} // namespace dxfio
