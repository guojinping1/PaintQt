#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <utility>

#include "SvgImport.h"

namespace svgio {
namespace {

const double PI = 3.14159265358979323846;

int rnd(double v)
{
    return v >= 0 ? int(v + 0.5) : -int(-v + 0.5);
}

// ---------------- 颜色 ----------------

// 返回 0xRRGGBB；none 为 true 表示 "none" / 不认识
unsigned parseColor(const QString& value, bool& none)
{
    none = false;

    QString s = value.trimmed().toLower();
    if (s.isEmpty() || s == "none" || s == "transparent")
    {
        none = true;
        return 0;
    }

    if (s.startsWith('#'))
    {
        s.remove(0, 1);
        if (s.size() == 3)                                  // #abc
            s = QString(s[0]) + s[0] + s[1] + s[1] + s[2] + s[2];
        if (s.size() >= 6)
            return unsigned(s.left(6).toUInt(nullptr, 16));
        none = true;
        return 0;
    }

    if (s.startsWith("rgb("))
    {
        QString in = s.mid(4);
        if (in.endsWith(')'))
            in.chop(1);

        QStringList parts;
        for (const QString& p : in.split(',', Qt::SkipEmptyParts))
            parts << p.trimmed();

        if (parts.size() == 3)
        {
            const unsigned r = std::clamp(parts[0].toInt(), 0, 255);
            const unsigned g = std::clamp(parts[1].toInt(), 0, 255);
            const unsigned b = std::clamp(parts[2].toInt(), 0, 255);
            return (r << 16) | (g << 8) | b;
        }
        none = true;
        return 0;
    }

    static const QHash<QString, unsigned> named = {
        { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 },
        { "green", 0x008000 }, { "lime", 0x00FF00 },  { "blue", 0x0000FF },
        { "yellow", 0xFFFF00 }, { "cyan", 0x00FFFF }, { "aqua", 0x00FFFF },
        { "magenta", 0xFF00FF }, { "fuchsia", 0xFF00FF }, { "gray", 0x808080 },
        { "grey", 0x808080 },  { "silver", 0xC0C0C0 }, { "maroon", 0x800000 },
        { "olive", 0x808000 }, { "navy", 0x000080 },  { "orange", 0xFFA500 },
        { "purple", 0x800080 }, { "teal", 0x008080 }, { "brown", 0xA52A2A },
    };

    const auto it = named.constFind(s);
    if (it != named.constEnd())
        return it.value();

    none = true;                                            // 不认识就当没有
    return 0;
}

// ---------------- 样式 ----------------

struct Style
{
    unsigned stroke = 0x000000;
    bool     hasStroke = true;
    double   width = 1.0;
};

// style="..." 的优先级高于 stroke="..." 这类表现属性，和 SVG 规范一致
void readStyle(const QXmlStreamAttributes& attrs, Style& st)
{
    QHash<QString, QString> css;
    if (attrs.hasAttribute("style"))
    {
        for (const QString& item : attrs.value("style").toString().split(';', Qt::SkipEmptyParts))
        {
            const int c = item.indexOf(':');
            if (c > 0)
                css.insert(item.left(c).trimmed().toLower(), item.mid(c + 1).trimmed());
        }
    }

    auto get = [&](const char* name) -> QString
    {
        const QString key = QString::fromLatin1(name);
        const auto it = css.constFind(key);
        if (it != css.constEnd())
            return it.value();
        if (attrs.hasAttribute(key))
            return attrs.value(key).toString();
        return QString();
    };

    const QString sv = get("stroke");
    if (!sv.isEmpty())
    {
        bool none = false;
        const unsigned c = parseColor(sv, none);
        st.hasStroke = !none;
        if (!none)
            st.stroke = c;
    }

    const QString wv = get("stroke-width");
    if (!wv.isEmpty())
    {
        bool ok = false;
        const double w = wv.toDouble(&ok);
        if (ok && w > 0)
            st.width = w;
    }

    // 我们只画轮廓。如果只给了 fill 没给 stroke，就拿 fill 的颜色当线色
    if (!st.hasStroke)
    {
        const QString fv = get("fill");
        if (!fv.isEmpty())
        {
            bool none = false;
            const unsigned c = parseColor(fv, none);
            if (!none)
            {
                st.stroke = c;
                st.hasStroke = true;
            }
        }
    }
}

// ---------------- 2x2 仿射变换 ----------------

struct Xform
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    // 没有旋转和斜切时，矩形/椭圆还能保持是矩形/椭圆
    bool axisAligned() const { return b == 0.0 && c == 0.0; }

    void apply(double& x, double& y) const
    {
        const double nx = a * x + c * y + e;
        const double ny = b * x + d * y + f;
        x = nx;
        y = ny;
    }

    // 先做 o，再做 this
    Xform multiply(const Xform& o) const
    {
        Xform r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e;
        r.f = b * o.e + d * o.f + f;
        return r;
    }
};

QStringList splitNums(const QString& s)
{
    QStringList out;
    QString cur;

    for (const QChar ch : s)
    {
        if (ch.isDigit() || ch == '.' || ch == '-' || ch == '+')
            cur += ch;
        else if (!cur.isEmpty())
        {
            out << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty())
        out << cur;

    return out;
}

// 把 "12px" / "1.5" 这类带单位的数值取出来
double num(const QString& s)
{
    QString t;
    for (const QChar c : s)
    {
        if (c.isDigit() || c == '.' || c == '-' || c == '+')
            t += c;
        else
            break;
    }
    return t.toDouble();
}

Xform parseTransform(const QString& s)
{
    Xform xf;
    int i = 0;

    while (i < s.size())
    {
        while (i < s.size() && !s[i].isLetter())
            i++;
        if (i >= s.size())
            break;

        const int ns = i;
        while (i < s.size() && s[i].isLetter())
            i++;
        const QString name = s.mid(ns, i - ns).toLower();

        while (i < s.size() && s[i] != '(')
            i++;
        if (i >= s.size())
            break;
        i++;

        int ne = s.indexOf(')', i);
        if (ne < 0)
            ne = s.size();

        const QStringList p = splitNums(s.mid(i, ne - i));
        i = ne + 1;

        const double v0 = p.size() > 0 ? p[0].toDouble() : 0;
        const double v1 = p.size() > 1 ? p[1].toDouble() : 0;
        const double v2 = p.size() > 2 ? p[2].toDouble() : 0;
        const double v3 = p.size() > 3 ? p[3].toDouble() : 0;
        const double v4 = p.size() > 4 ? p[4].toDouble() : 0;
        const double v5 = p.size() > 5 ? p[5].toDouble() : 0;

        Xform t;

        if (name == "translate")
        {
            t.e = v0;
            t.f = v1;
        }
        else if (name == "scale")
        {
            t.a = v0;
            t.d = (p.size() > 1) ? v1 : v0;
        }
        else if (name == "matrix")
        {
            t.a = v0; t.b = v1; t.c = v2;
            t.d = v3; t.e = v4; t.f = v5;
        }
        else if (name == "rotate")
        {
            const double rad = v0 * PI / 180.0;
            const double cs = std::cos(rad), sn = std::sin(rad);
            t.a = cs; t.b = sn; t.c = -sn; t.d = cs;

            if (p.size() >= 3)                      // rotate(a cx cy)
            {
                Xform to, back;
                to.e = v1;   to.f = v2;
                back.e = -v1; back.f = -v2;
                t = to.multiply(t).multiply(back);
            }
        }
        else if (name == "skewx")
        {
            t.c = std::tan(v0 * PI / 180.0);
        }
        else if (name == "skewy")
        {
            t.b = std::tan(v0 * PI / 180.0);
        }
        else
        {
            continue;                               // 不认识的变换，跳过
        }

        xf = xf.multiply(t);
    }

    return xf;
}

// ---------------- path 的 d 属性 ----------------

class PathScanner
{
public:
    explicit PathScanner(const QString& s) : m_s(s) {}

    bool atEnd()
    {
        skipSep();
        return m_i >= m_s.size();
    }

    QChar peek()
    {
        skipSep();
        return m_i < m_s.size() ? m_s[m_i] : QChar();
    }

    QChar takeCmd()
    {
        skipSep();
        return m_s[m_i++];
    }

    double number()
    {
        skipSep();
        const int st = m_i;

        if (m_i < m_s.size() && (m_s[m_i] == '-' || m_s[m_i] == '+'))
            m_i++;
        while (m_i < m_s.size() && (m_s[m_i].isDigit() || m_s[m_i] == '.'))
            m_i++;
        if (m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E'))
        {
            m_i++;
            if (m_i < m_s.size() && (m_s[m_i] == '-' || m_s[m_i] == '+'))
                m_i++;
            while (m_i < m_s.size() && m_s[m_i].isDigit())
                m_i++;
        }

        return m_s.mid(st, m_i - st).toDouble();
    }

private:
    void skipSep()
    {
        while (m_i < m_s.size() && (m_s[m_i].isSpace() || m_s[m_i] == ','))
            m_i++;
    }

    const QString& m_s;
    int m_i = 0;
};

using DPoint = std::pair<double, double>;

double dist(double x0, double y0, double x1, double y1)
{
    return std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
}

// 三次贝塞尔按长度离散成折线
void addCubic(std::vector<DPoint>& v,
              double x0, double y0, double x1, double y1,
              double x2, double y2, double x3, double y3)
{
    const double len = dist(x0, y0, x1, y1) + dist(x1, y1, x2, y2) + dist(x2, y2, x3, y3);
    const int n = std::clamp(int(len / 3.0) + 2, 2, 64);

    for (int i = 1; i <= n; ++i)
    {
        const double t = double(i) / n;
        const double u = 1 - t;
        const double x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
        const double y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
        v.emplace_back(x, y);
    }
}

void addQuad(std::vector<DPoint>& v,
             double x0, double y0, double x1, double y1, double x2, double y2)
{
    const double len = dist(x0, y0, x1, y1) + dist(x1, y1, x2, y2);
    const int n = std::clamp(int(len / 3.0) + 2, 2, 64);

    for (int i = 1; i <= n; ++i)
    {
        const double t = double(i) / n;
        const double u = 1 - t;
        const double x = u * u * x0 + 2 * u * t * x1 + t * t * x2;
        const double y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
        v.emplace_back(x, y);
    }
}

// 把 path 拆成若干条折线
std::vector<std::vector<DPoint>> parsePathData(const QString& d, int& arcCount)
{
    std::vector<std::vector<DPoint>> out;
    std::vector<DPoint> cur;

    PathScanner sc(d);
    QChar cmd;

    double cx = 0, cy = 0;          // 当前点
    double sx = 0, sy = 0;          // 子路径起点
    double lx = 0, ly = 0;          // 上一个控制点（给 S / T 用）
    QChar  lastCmd;

    auto flush = [&]()
    {
        if (cur.size() >= 2)
            out.push_back(cur);
        cur.clear();
    };

    while (!sc.atEnd())
    {
        if (sc.peek().isLetter())
            cmd = sc.takeCmd();
        else if (cmd.isNull())
            break;                  // 还没出现过命令，忽略

        const QChar thisCmd = cmd;
        const bool  rel = thisCmd.isLower();
        const QChar C = thisCmd.toUpper();

        if (C == 'M')
        {
            const double x = sc.number(), y = sc.number();
            cx = rel ? cx + x : x;
            cy = rel ? cy + y : y;
            flush();
            sx = cx;  sy = cy;
            cur.emplace_back(cx, cy);
            cmd = rel ? QChar('l') : QChar('L');    // 后面跟的数字当成 lineto
        }
        else if (C == 'L')
        {
            const double x = sc.number(), y = sc.number();
            cx = rel ? cx + x : x;
            cy = rel ? cy + y : y;
            cur.emplace_back(cx, cy);
        }
        else if (C == 'H')
        {
            const double x = sc.number();
            cx = rel ? cx + x : x;
            cur.emplace_back(cx, cy);
        }
        else if (C == 'V')
        {
            const double y = sc.number();
            cy = rel ? cy + y : y;
            cur.emplace_back(cx, cy);
        }
        else if (C == 'C' || C == 'S')
        {
            double ax1, ay1;

            if (C == 'C')
            {
                const double x1 = sc.number(), y1 = sc.number();
                ax1 = rel ? cx + x1 : x1;
                ay1 = rel ? cy + y1 : y1;
            }
            else if (lastCmd.toUpper() == 'C' || lastCmd.toUpper() == 'S')
            {
                ax1 = 2 * cx - lx;      // 对称反射
                ay1 = 2 * cy - ly;
            }
            else
            {
                ax1 = cx;
                ay1 = cy;
            }

            const double x2 = sc.number(), y2 = sc.number();
            const double x = sc.number(), y = sc.number();
            const double ax2 = rel ? cx + x2 : x2;
            const double ay2 = rel ? cy + y2 : y2;
            const double ax = rel ? cx + x : x;
            const double ay = rel ? cy + y : y;

            addCubic(cur, cx, cy, ax1, ay1, ax2, ay2, ax, ay);
            lx = ax2;  ly = ay2;
            cx = ax;   cy = ay;
        }
        else if (C == 'Q' || C == 'T')
        {
            double ax1, ay1;

            if (C == 'Q')
            {
                const double x1 = sc.number(), y1 = sc.number();
                ax1 = rel ? cx + x1 : x1;
                ay1 = rel ? cy + y1 : y1;
            }
            else if (lastCmd.toUpper() == 'Q' || lastCmd.toUpper() == 'T')
            {
                ax1 = 2 * cx - lx;
                ay1 = 2 * cy - ly;
            }
            else
            {
                ax1 = cx;
                ay1 = cy;
            }

            const double x = sc.number(), y = sc.number();
            const double ax = rel ? cx + x : x;
            const double ay = rel ? cy + y : y;

            addQuad(cur, cx, cy, ax1, ay1, ax, ay);
            lx = ax1;  ly = ay1;
            cx = ax;   cy = ay;
        }
        else if (C == 'A')
        {
            // 圆弧：这里简化成一条直线到终点
            sc.number(); sc.number(); sc.number();
            sc.number(); sc.number();
            const double x = sc.number(), y = sc.number();

            cx = rel ? cx + x : x;
            cy = rel ? cy + y : y;
            cur.emplace_back(cx, cy);
            arcCount++;
        }
        else if (C == 'Z')
        {
            if (!cur.empty())
                cur.emplace_back(sx, sy);
            flush();
            cx = sx;
            cy = sy;
        }
        else
        {
            break;                      // 不认识的命令，停止解析
        }

        lastCmd = thisCmd;
    }

    flush();
    return out;
}

// ---------------- 解析器 ----------------

class SvgParser
{
public:
    Result run(QIODevice* dev)
    {
        QXmlStreamReader xml(dev);

        bool foundSvg = false;
        while (!xml.atEnd())
        {
            xml.readNext();
            if (!xml.isStartElement())
                continue;

            const QString name = xml.name().toString().toLower();
            if (name != "svg")
                continue;               // 跳过注释、<?xml?>、DOCTYPE 等

            foundSvg = true;
            readSvgAttrs(xml.attributes());

            const Style st;
            while (xml.readNextStartElement())
                parseNode(xml, st, m_root);
            break;
        }

        if (xml.hasError())
        {
            m_res.error = xml.errorString();
            return m_res;
        }
        if (!foundSvg)
        {
            m_res.error = QStringLiteral("文件里没有找到 <svg> 根元素");
            return m_res;
        }

        m_res.ok = true;
        return m_res;
    }

private:
    // <svg width height viewBox>：算出 viewBox 到画布的变换
    void readSvgAttrs(const QXmlStreamAttributes& a)
    {
        double w = 0, h = 0;
        if (a.hasAttribute("width"))
            w = num(a.value("width").toString());
        if (a.hasAttribute("height"))
            h = num(a.value("height").toString());

        if (a.hasAttribute("viewBox"))
        {
            const QStringList p = splitNums(a.value("viewBox").toString());
            if (p.size() == 4)
            {
                const double vx = p[0].toDouble(), vy = p[1].toDouble();
                const double vw = p[2].toDouble(), vh = p[3].toDouble();

                if (vw > 0 && vh > 0)
                {
                    if (w <= 0 || h <= 0)
                    {
                        w = vw;
                        h = vh;
                    }

                    Xform t;
                    t.a = w / vw;
                    t.d = h / vh;
                    t.e = -vx * t.a;
                    t.f = -vy * t.d;
                    m_root = t;
                }
            }
        }

        m_res.width = w;
        m_res.height = h;
    }

    void parseNode(QXmlStreamReader& xml, Style st, Xform xf)
    {
        const QString name = xml.name().toString().toLower();
        const QXmlStreamAttributes a = xml.attributes();

        readStyle(a, st);
        if (a.hasAttribute("transform"))
            xf = xf.multiply(parseTransform(a.value("transform").toString()));

        // 容器：继续往下走
        if (name == "g" || name == "svg" || name == "a" || name == "switch")
        {
            while (xml.readNextStartElement())
                parseNode(xml, st, xf);
            return;
        }

        // 这些是定义或不渲染的东西，整棵子树跳过
        if (name == "defs" || name == "symbol" || name == "clipPath" ||
            name == "mask" || name == "pattern" || name == "marker" ||
            name == "linearGradient" || name == "radialGradient" ||
            name == "style" || name == "title" || name == "desc" ||
            name == "metadata" || name == "use")
        {
            xml.skipCurrentElement();
            return;
        }

        if (!st.hasStroke)
        {
            m_res.skipped++;
            xml.skipCurrentElement();
            return;
        }

        if (name == "line")
            makeLine(a, st, xf);
        else if (name == "rect")
            makeRect(a, st, xf);
        else if (name == "circle")
            makeEllipse(a, st, xf, true);
        else if (name == "ellipse")
            makeEllipse(a, st, xf, false);
        else if (name == "polygon")
            makePoly(a, st, xf, true);
        else if (name == "polyline")
            makePoly(a, st, xf, false);
        else if (name == "path")
            makePath(a, st, xf);
        else
            m_res.skipped++;

        xml.skipCurrentElement();
    }

    static double attr(const QXmlStreamAttributes& a, const char* name)
    {
        return a.hasAttribute(QLatin1String(name)) ? a.value(QLatin1String(name)).toString().toDouble() : 0.0;
    }

    static void applyStyle(draw::Shape& s, const Style& st)
    {
        s.color = st.stroke;
        s.width = std::max(1, rnd(st.width));
    }

    void push(draw::Shape& s, const Style& st)
    {
        applyStyle(s, st);
        m_res.shapes.push_back(s);
    }

    void makeLine(const QXmlStreamAttributes& a, const Style& st, const Xform& xf)
    {
        double x1 = attr(a, "x1"), y1 = attr(a, "y1");
        double x2 = attr(a, "x2"), y2 = attr(a, "y2");
        xf.apply(x1, y1);
        xf.apply(x2, y2);

        draw::Shape s;
        s.type = draw::ShapeType::Line;
        s.x1 = rnd(x1);  s.y1 = rnd(y1);
        s.x2 = rnd(x2);  s.y2 = rnd(y2);
        push(s, st);
    }

    void makeRect(const QXmlStreamAttributes& a, const Style& st, const Xform& xf)
    {
        const double x = attr(a, "x"), y = attr(a, "y");
        const double w = attr(a, "width"), h = attr(a, "height");
        if (w <= 0 || h <= 0)
            return;

        if (xf.axisAligned())
        {
            double x1 = x, y1 = y, x2 = x + w, y2 = y + h;
            xf.apply(x1, y1);
            xf.apply(x2, y2);

            draw::Shape s;
            s.type = draw::ShapeType::Rect;
            s.x1 = rnd(x1);  s.y1 = rnd(y1);
            s.x2 = rnd(x2);  s.y2 = rnd(y2);
            push(s, st);
            return;
        }

        // 被旋转/斜切过，用闭合折线
        const double px[4] = { x, x + w, x + w, x };
        const double py[4] = { y, y,     y + h, y + h };

        draw::Shape s;
        s.type = draw::ShapeType::Polyline;
        s.closed = true;
        for (int i = 0; i < 4; ++i)
        {
            double u = px[i], v = py[i];
            xf.apply(u, v);
            s.pts.push_back({ rnd(u), rnd(v) });
        }
        push(s, st);
    }

    void makeEllipse(const QXmlStreamAttributes& a, const Style& st, const Xform& xf, bool isCircle)
    {
        const double cx = attr(a, "cx"), cy = attr(a, "cy");
        double rx, ry;

        if (isCircle)
        {
            rx = ry = attr(a, "r");
        }
        else
        {
            rx = attr(a, "rx");
            ry = attr(a, "ry");
        }

        if (rx <= 0 || ry <= 0)
            return;

        if (xf.axisAligned())
        {
            double x1 = cx - rx, y1 = cy - ry, x2 = cx + rx, y2 = cy + ry;
            xf.apply(x1, y1);
            xf.apply(x2, y2);

            draw::Shape s;
            s.type = draw::ShapeType::Ellipse;
            s.x1 = rnd(x1);  s.y1 = rnd(y1);
            s.x2 = rnd(x2);  s.y2 = rnd(y2);
            push(s, st);
            return;
        }

        draw::Shape s;
        s.type = draw::ShapeType::Polyline;
        s.closed = true;
        for (int i = 0; i < 64; ++i)
        {
            const double t = 2 * PI * i / 64.0;
            double u = cx + rx * std::cos(t);
            double v = cy + ry * std::sin(t);
            xf.apply(u, v);
            s.pts.push_back({ rnd(u), rnd(v) });
        }
        push(s, st);
    }

    void makePoly(const QXmlStreamAttributes& a, const Style& st, const Xform& xf, bool closed)
    {
        const QStringList n = splitNums(a.value("points").toString());
        if (n.size() < 4)
            return;

        draw::Shape s;
        s.type = draw::ShapeType::Polyline;
        s.closed = closed;

        for (int i = 0; i + 1 < n.size(); i += 2)
        {
            double x = n[i].toDouble(), y = n[i + 1].toDouble();
            xf.apply(x, y);
            s.pts.push_back({ rnd(x), rnd(y) });
        }

        if (s.pts.size() >= 2)
            push(s, st);
    }

    void makePath(const QXmlStreamAttributes& a, const Style& st, const Xform& xf)
    {
        const QString d = a.value("d").toString();
        if (d.isEmpty())
            return;

        int arcs = 0;
        const std::vector<std::vector<DPoint>> polys = parsePathData(d, arcs);
        m_res.arcs += arcs;

        for (const std::vector<DPoint>& poly : polys)
        {
            draw::Shape s;
            s.type = draw::ShapeType::Polyline;

            for (const DPoint& p : poly)
            {
                double x = p.first, y = p.second;
                xf.apply(x, y);
                s.pts.push_back({ rnd(x), rnd(y) });
            }

            if (s.pts.size() >= 2)
                push(s, st);
        }
    }

    Result  m_res;
    Xform   m_root;
};

} // namespace

Result loadData(const QByteArray& data)
{
    QBuffer buf;
    buf.setData(data);

    if (!buf.open(QIODevice::ReadOnly))
    {
        Result r;
        r.error = QStringLiteral("内存数据打不开");
        return r;
    }

    SvgParser parser;
    return parser.run(&buf);
}

Result loadFile(const QString& fileName)
{
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        Result r;
        r.error = QStringLiteral("打不开文件：%1").arg(fileName);
        return r;
    }

    SvgParser parser;
    return parser.run(&f);
}

} // namespace svgio
