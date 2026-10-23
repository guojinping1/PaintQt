#pragma once

#include <QGraphicsView>

// 画布视图：白纸 + 浅网格 + 纸张边界。
// Ctrl + 滚轮缩放；选择工具下支持框选。
class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit CanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);

    // 选择工具时打开橡皮筋框选，其他绘图工具下是十字光标
    void setSelectMode(bool on);

    qreal zoom() const { return m_zoom; }
    void  resetZoom();
    void  fitPage();          // 缩放到刚好放下整张画布

signals:
    void zoomChanged(qreal zoom);

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    qreal m_zoom = 1.0;
};
