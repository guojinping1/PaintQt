// ============================================================================
//  bitmap 库自测程序
//
//  生成一张测试图，把 BitmapOps 里每个功能都跑一遍并存成 PNG，
//  用来验证 OpenCV 的链接与运行是否正常 —— 不需要开界面。
//
//  用法：bitmap_selftest.exe <输出目录>
//  输出：01-original.png / 02-gray.png / ... / 09-wand-trimmed.png
// ============================================================================

#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QTextStream>

#include "BitmapOps.h"

namespace {

QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}

// 造一张有渐变、有色块、有圆的测试图。
// 色块是为了测抠图容差：从红块中心点一下，应该正好把红块抠出来。
QImage makeTestImage(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient grad(0, 0, w, h);
    grad.setColorAt(0.0, QColor(60, 120, 220));
    grad.setColorAt(1.0, QColor(240, 200, 80));
    p.fillRect(img.rect(), grad);

    p.fillRect(40, 40, 120, 100, QColor(220, 60, 60));            // 红块
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 180, 90));
    p.drawEllipse(QPoint(w - 140, h / 2), 90, 90);                // 绿圆

    p.end();
    return img;
}

bool saveTo(const QDir& dir, const char* name, const QImage& img)
{
    if (img.isNull())
    {
        out() << "  [失败] " << name << " —— 输出为空图\n";
        return false;
    }

    const QString path = dir.filePath(QString::fromLatin1(name));
    const bool ok = img.save(path);

    out() << "  [" << (ok ? "OK  " : "失败") << "] " << name
          << "  " << img.width() << "x" << img.height() << "\n";
    return ok;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    const QString outDirPath = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                          : QStringLiteral("selftest_out");
    QDir dir(outDirPath);
    if (!dir.exists() && !QDir().mkpath(outDirPath))
    {
        out() << "无法创建输出目录: " << outDirPath << "\n";
        return 1;
    }

    out() << "输出目录: " << dir.absolutePath() << "\n\n";

    const QImage src = makeTestImage(480, 320);
    int pass = 0;
    int total = 0;

    auto run = [&](const char* name, const QImage& result)
    {
        ++total;
        if (saveTo(dir, name, result))
            ++pass;
    };

    out() << "--- 位图编辑 ---\n";
    run("01-original.png", src);
    run("02-gray.png",     bitmap::toGray(src));
    run("03-binary.png",   bitmap::toBinary(src, 128));
    run("04-blur.png",     bitmap::gaussianBlur(src, 6));
    run("05-edge.png",     bitmap::edgeDetect(src, 50, 150));
    run("06-invert.png",   bitmap::invert(src));
    run("07-bright.png",   bitmap::adjustBrightness(src, 40));

    out() << "\n--- 抠图 ---\n";

    // 从红块中心 (100, 90) 点一下，容差 40
    QRect bounds;
    const QImage cut = bitmap::magicWand(src, QPoint(100, 90), 40, &bounds);
    run("08-wand.png", cut);

    out() << "  选中区域外接矩形: "
          << bounds.x() << "," << bounds.y() << " "
          << bounds.width() << "x" << bounds.height()
          << "   （预期约 40,40 120x100）\n";

    QRect trimmedBounds;
    run("09-wand-trimmed.png", bitmap::trimTransparent(cut, &trimmedBounds));
    out() << "  裁剪后尺寸: "
          << trimmedBounds.width() << "x" << trimmedBounds.height() << "\n";

    run("10-wand-invert.png", bitmap::magicWandInvert(src, QPoint(100, 90), 40));

    out() << "\n结果: " << pass << " / " << total << " 项成功\n";
    return (pass == total) ? 0 : 2;
}
