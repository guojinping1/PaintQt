#include "BitmapOps.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <functional>
#include <vector>

namespace bitmap {

namespace {

// QImage → cv::Mat（统一转成 BGRA 四通道）
//
// 注意必须 clone()：下面构造的 cv::Mat 只是"包着" QImage 的内存，
// 函数返回后局部 QImage 就析构了，不 clone 就会拿到野指针。
cv::Mat toMat(const QImage& img)
{
    if (img.isNull())
        return cv::Mat();

    const QImage src = img.convertToFormat(QImage::Format_ARGB32);

    cv::Mat view(src.height(), src.width(), CV_8UC4,
                 const_cast<uchar*>(src.constBits()),
                 size_t(src.bytesPerLine()));

    return view.clone();
}

// cv::Mat → QImage
QImage toImage(const cv::Mat& mat)
{
    if (mat.empty())
        return QImage();

    if (mat.type() == CV_8UC4)
    {
        return QImage(mat.data, mat.cols, mat.rows, int(mat.step),
                      QImage::Format_ARGB32).copy();
    }

    if (mat.type() == CV_8UC3)
    {
        cv::Mat bgra;
        cv::cvtColor(mat, bgra, cv::COLOR_BGR2BGRA);
        return QImage(bgra.data, bgra.cols, bgra.rows, int(bgra.step),
                      QImage::Format_ARGB32).copy();
    }

    if (mat.type() == CV_8UC1)
    {
        cv::Mat bgra;
        cv::cvtColor(mat, bgra, cv::COLOR_GRAY2BGRA);
        return QImage(bgra.data, bgra.cols, bgra.rows, int(bgra.step),
                      QImage::Format_ARGB32).copy();
    }

    return QImage();
}

// 只对 BGR 三个颜色通道做变换，alpha 原样保留。
//
// 为什么要拆开？因为把整张 BGRA 一起做位运算/算术运算会把 alpha 也算进去：
// bitwise_not 会把 255 的 alpha 翻成 0，整张图直接变全透明；
// convertTo 加亮度也会顺带给 alpha 加上去，半透明边缘会变形。
cv::Mat mapColorChannels(const cv::Mat& bgra, const std::function<void(cv::Mat&)>& fn)
{
    std::vector<cv::Mat> ch;
    cv::split(bgra, ch);        // 拆成 B、G、R、A 四个单通道

    fn(ch[0]);
    fn(ch[1]);
    fn(ch[2]);
    // ch[3] 是 alpha，不碰

    cv::Mat out;
    cv::merge(ch, out);
    return out;
}

} // namespace

// ---------------- 位图编辑 ----------------

QImage toGray(const QImage& src)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    cv::Mat gray;
    cv::cvtColor(m, gray, cv::COLOR_BGRA2GRAY);   // 单通道

    return toImage(gray);                          // toImage 会转回 BGRA 显示
}

QImage toBinary(const QImage& src, int thresholdValue)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    cv::Mat gray;
    cv::cvtColor(m, gray, cv::COLOR_BGRA2GRAY);

    const int t = std::clamp(thresholdValue, 0, 255);

    cv::Mat bin;
    cv::threshold(gray, bin, t, 255, cv::THRESH_BINARY);

    return toImage(bin);
}

QImage gaussianBlur(const QImage& src, int radius)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    // 高斯核的边长必须是奇数，且至少 1
    const int r = std::clamp(radius, 0, 30);
    const int k = r * 2 + 1;

    cv::Mat out;
    cv::GaussianBlur(m, out, cv::Size(k, k), 0);

    return toImage(out);
}

QImage edgeDetect(const QImage& src, int lowThreshold, int highThreshold)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    cv::Mat gray;
    cv::cvtColor(m, gray, cv::COLOR_BGRA2GRAY);

    // Canny 要求 low < high，这里做个保护，避免用户传反了
    int lo = std::clamp(lowThreshold, 1, 254);
    int hi = std::clamp(highThreshold, lo + 1, 255);

    cv::Mat edges;
    cv::Canny(gray, edges, lo, hi);

    return toImage(edges);
}

QImage adjustBrightness(const QImage& src, int delta)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    // alpha=1 保持对比度，beta=delta 是加在亮度上的偏移；OpenCV 会自动饱和截断。
    // 用 mapColorChannels 保证 alpha 不被加上去。
    const double d = double(std::clamp(delta, -255, 255));

    const cv::Mat out = mapColorChannels(m, [d](cv::Mat& c)
    {
        c.convertTo(c, -1, 1.0, d);
    });

    return toImage(out);
}

QImage invert(const QImage& src)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    // 只反相颜色通道。直接 bitwise_not 整个 BGRA 会把 alpha 翻成 0，
    // 结果是一张全透明的图（这个 bug 是靠肉眼看拼图才发现的）
    const cv::Mat out = mapColorChannels(m, [](cv::Mat& c)
    {
        cv::bitwise_not(c, c);
    });

    return toImage(out);
}

// ---------------- 抠图 ----------------

QImage magicWand(const QImage& src, const QPoint& seed, int tolerance, QRect* outMaskBounds)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= m.cols || seed.y() >= m.rows)
        return src;

    // ① floodFill 只接受 1 或 3 通道的图像，而这里是 BGRA 四通道，
    //    所以先转成 BGR 再处理。原图 m 保留着，后面应用掩码要用它（保住 alpha）。
    cv::Mat bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR);

    //    mask 要比原图大一圈（OpenCV 的 floodFill 要求），所以 +2。
    cv::Mat mask = cv::Mat::zeros(m.rows + 2, m.cols + 2, CV_8UC1);
    cv::Mat flood = bgr.clone();        // floodFill 会改这个图，所以传个副本

    const int tol = std::clamp(tolerance, 0, 255);
    const cv::Scalar lo(tol, tol, tol);         // 三通道
    const cv::Scalar up(tol, tol, tol);

    // FLOODFILL_MASK_ONLY：只填掩码不改图；255<<8 是填进掩码的值
    cv::floodFill(flood, mask, cv::Point(seed.x(), seed.y()),
                  cv::Scalar(0), nullptr, lo, up,
                  8 | cv::FLOODFILL_MASK_ONLY | (255 << 8));

    // ② 把外边那一圈边框切掉，得到和原图同尺寸的选区
    const cv::Mat sel = mask(cv::Rect(1, 1, m.cols, m.rows));

    // ③ 选区之外全部置为完全透明
    cv::Mat keep = m.clone();
    cv::Mat outside;
    cv::bitwise_not(sel, outside);
    keep.setTo(cv::Scalar(0, 0, 0, 0), outside);

    // ④ 顺带算出选中区域的外接矩形
    if (outMaskBounds)
    {
        std::vector<cv::Point> pts;
        cv::findNonZero(sel, pts);

        if (pts.empty())
            *outMaskBounds = QRect();
        else
        {
            const cv::Rect r = cv::boundingRect(pts);
            *outMaskBounds = QRect(r.x, r.y, r.width, r.height);
        }
    }

    return toImage(keep);
}

QImage magicWandInvert(const QImage& src, const QPoint& seed, int tolerance)
{
    cv::Mat m = toMat(src);
    if (m.empty())
        return src;

    if (seed.x() < 0 || seed.y() < 0 || seed.x() >= m.cols || seed.y() >= m.rows)
        return src;

    // 同样：floodFill 只吃 1 或 3 通道，先转 BGR
    cv::Mat bgr;
    cv::cvtColor(m, bgr, cv::COLOR_BGRA2BGR);

    cv::Mat mask = cv::Mat::zeros(m.rows + 2, m.cols + 2, CV_8UC1);
    cv::Mat flood = bgr.clone();

    const int tol = std::clamp(tolerance, 0, 255);
    const cv::Scalar lo(tol, tol, tol);
    const cv::Scalar up(tol, tol, tol);

    cv::floodFill(flood, mask, cv::Point(seed.x(), seed.y()),
                  cv::Scalar(0), nullptr, lo, up,
                  8 | cv::FLOODFILL_MASK_ONLY | (255 << 8));

    const cv::Mat sel = mask(cv::Rect(1, 1, m.cols, m.rows));

    // 和魔棒相反：选区本身置透明，保留外面
    cv::Mat keep = m.clone();
    keep.setTo(cv::Scalar(0, 0, 0, 0), sel);

    return toImage(keep);
}

QImage trimTransparent(const QImage& src, QRect* outBounds)
{
    if (src.isNull())
        return src;

    cv::Mat m = toMat(src);

    cv::Mat alpha;
    cv::extractChannel(m, alpha, 3);        // 第 4 个通道 = 透明度

    std::vector<cv::Point> pts;
    cv::findNonZero(alpha, pts);            // 所有非全透明像素的位置

    if (pts.empty())
    {
        if (outBounds)
            *outBounds = QRect();
        return src;
    }

    const cv::Rect r = cv::boundingRect(pts);

    if (outBounds)
        *outBounds = QRect(r.x, r.y, r.width, r.height);

    return src.copy(r.x, r.y, r.width, r.height);
}

} // namespace bitmap
