#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

#include "BitmapItem.h"
#include "CanvasScene.h"
#include "CanvasView.h"
#include "SvgImport.h"

#ifdef PAINTQT_HAVE_OPENCV
#include "BitmapOps.h"
#endif

// ---------------------------------------------------------------------------
// 界面样式。配色和圆角都集中在这里，控件本身不带视觉样式。
// ---------------------------------------------------------------------------
static const char* kStyleSheet = R"(
QWidget#SidePanel {
    background: #F6F8FB;
}
QFrame#Sep {
    background: #E3E7EE;
    max-height: 1px;
}
QLabel#SectionTitle {
    color: #808C9E;
    font-size: 11px;
    font-weight: 600;
}
QLabel#AppTitle {
    color: #26303D;
    font-size: 15px;
    font-weight: 600;
}
QLabel#Hint {
    color: #6B7789;
    font-size: 11px;
}
QToolButton#Tool {
    border: 1px solid transparent;
    border-radius: 7px;
    background: transparent;
}
QToolButton#Tool:hover {
    background: #E9EEF7;
    border-color: #D2DCEA;
}
QToolButton#Tool:checked {
    background: #DBE8FB;
    border-color: #1E78DC;
}
QToolButton#Action {
    border: 1px solid #DCE1E9;
    border-radius: 7px;
    padding: 5px 8px;
    background: #FFFFFF;
    color: #3C4653;
    font-size: 12px;
}
QToolButton#Action:hover {
    background: #EEF3FB;
    border-color: #B7CBE5;
}
QToolButton#Swatch {
    border: 1px solid #C9D1DB;
    border-radius: 7px;
    padding: 2px;
}
QToolButton#Swatch:checked {
    border: 2px solid #1E78DC;
}
)";

namespace {

QIcon iconOf(const char* name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(QLatin1String(name)));
}

// 颜色块图标直接用 QPainter 画，不用额外做图片资源
QIcon swatchIcon(unsigned c)
{
    QPixmap pm(20, 20);
    pm.fill(QColor(int((c >> 16) & 0xFF), int((c >> 8) & 0xFF), int(c & 0xFF)));
    return QIcon(pm);
}

QFrame* makeSeparator(QWidget* parent)
{
    auto* f = new QFrame(parent);
    f->setObjectName(QStringLiteral("Sep"));
    f->setFixedHeight(1);
    return f;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("SectionTitle"));
    return l;
}

QString hintFor(CanvasScene::Tool t)
{
    switch (t)
    {
    case CanvasScene::Tool::Select:
        return QStringLiteral("单击图形选中，拖动白色圆点即可编辑形状；按 Delete 删除选中图形。");
    case CanvasScene::Tool::Bezier:
        return QStringLiteral("拖出一条曲线，再用「选择」工具拖动控制点调整弧度。");
    case CanvasScene::Tool::Pencil:
        return QStringLiteral("按住鼠标左键自由绘制。");
    case CanvasScene::Tool::Eraser:
        return QStringLiteral("按住鼠标左键擦除，松开结束。");
    case CanvasScene::Tool::Wand:
        return QStringLiteral("魔棒：在位图上点一下，会把颜色相近的区域保留下来，其余变透明。");
    default:
        return QStringLiteral("在画布上按住左键拖拽即可绘制。");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 主窗口：左右分栏。左边功能区全用代码搭，工程里没有 .ui 文件。
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow
{
public:
    MainWindow()
    {
        m_scene = new CanvasScene(this);
        m_view = new CanvasView(m_scene, this);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->addWidget(buildSidePanel());
        splitter->addWidget(m_view);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setCollapsible(0, false);
        splitter->setSizes({ 190, 900 });
        setCentralWidget(splitter);

        setWindowTitle(QStringLiteral("画图板 - Qt 版"));
        resize(1200, 820);

        m_countLabel = new QLabel(this);
        m_zoomLabel = new QLabel(this);
        m_zoomLabel->setMinimumWidth(90);
        statusBar()->addPermanentWidget(m_countLabel);
        statusBar()->addPermanentWidget(m_zoomLabel);
        statusBar()->showMessage(QStringLiteral("就绪"));

        connect(m_scene, &CanvasScene::countChanged, this, [this](int n)
        {
            m_countLabel->setText(QStringLiteral("图形：%1").arg(n));
        });
        connect(m_scene, &CanvasScene::statusMessage, this, [this](const QString& s)
        {
            statusBar()->showMessage(s, 4000);
        });
        connect(m_view, &CanvasView::zoomChanged, this, [this](qreal z)
        {
            m_zoomLabel->setText(QStringLiteral("缩放：%1%").arg(int(z * 100)));
        });

#ifdef PAINTQT_HAVE_OPENCV
        // 魔棒被点了：拿到种子点，做抠图
        connect(m_scene, &CanvasScene::wandPicked, this,
                [this](BitmapItem* item, const QPoint& pixel)
        {
            if (!item)
                return;

            QRect bounds;
            const QImage cut = bitmap::magicWand(item->current(), pixel, m_wandTolerance, &bounds);
            item->applyFilter(cut);

            selectTool(int(CanvasScene::Tool::Select));

            statusBar()->showMessage(
                QStringLiteral("已抠图：种子点 (%1, %2)，容差 %3，选区 %4×%5")
                    .arg(pixel.x()).arg(pixel.y())
                    .arg(m_wandTolerance)
                    .arg(bounds.width()).arg(bounds.height()),
                6000);
        });
#endif

        m_countLabel->setText(QStringLiteral("图形：0"));
        m_zoomLabel->setText(QStringLiteral("缩放：100%"));

        addShortcut(QKeySequence::Undo, [this] { m_scene->undo(); });
        addShortcut(QKeySequence::Delete, [this] { m_scene->deleteSelected(); });
        addShortcut(QKeySequence::Save, [this] { savePng(); });

        selectTool(int(CanvasScene::Tool::Line));
    }

    // 缩放到刚好放下整张画布
    void fitPage() { m_view->fitPage(); }

    // 供命令行参数调用：直接打开一个 SVG
    void importFile(const QString& path)
    {
        const svgio::Result r = svgio::loadFile(path);

        if (!r.ok || r.shapes.empty())
        {
            QMessageBox::warning(this, QStringLiteral("导入失败"),
                                 r.error.isEmpty()
                                     ? QStringLiteral("这个 SVG 里没有可导入的描边图形")
                                     : r.error);
            return;
        }

        m_scene->addShapes(r.shapes, true);
        m_view->fitPage();          // 让导入的内容整屏可见

        QString msg = QStringLiteral("已导入 %1 个图形").arg(int(r.shapes.size()));
        if (r.skipped > 0)
            msg += QStringLiteral("，跳过 %1 个不支持的元素").arg(r.skipped);
        if (r.arcs > 0)
            msg += QStringLiteral("，%1 段圆弧已简化为直线").arg(r.arcs);

        statusBar()->showMessage(msg, 6000);
    }

    // 供命令行参数调用：直接打开一张位图
    void openBitmapFile(const QString& path)
    {
#ifdef PAINTQT_HAVE_OPENCV
        const QImage img(path);
        if (img.isNull())
        {
            QMessageBox::warning(this, QStringLiteral("打开失败"),
                                 QStringLiteral("无法读取这张图片：\n%1").arg(path));
            return;
        }

        m_scene->addBitmap(img);
        m_view->fitPage();
#else
        Q_UNUSED(path);
#endif
    }

private:
    // ------------------------------ 左侧功能区 ------------------------------

    QWidget* buildSidePanel()
    {
        auto* panel = new QWidget(this);
        panel->setObjectName(QStringLiteral("SidePanel"));
        panel->setStyleSheet(QLatin1String(kStyleSheet));
        panel->setMinimumWidth(172);
        panel->setMaximumWidth(230);

        auto* v = new QVBoxLayout(panel);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(8);

        // —— 标题 ——
        auto* title = new QLabel(QStringLiteral("画图板"), panel);
        title->setObjectName(QStringLiteral("AppTitle"));
        v->addWidget(title);
        v->addWidget(makeSeparator(panel));

        // —— 绘图工具 ——
        v->addWidget(makeSectionTitle(QStringLiteral("绘图工具"), panel));

        struct ToolDef
        {
            const char*       icon;
            QString           tip;
            CanvasScene::Tool tool;
        };

        const ToolDef tools[] =
        {
            { "select",   QStringLiteral("选择 / 编辑节点"), CanvasScene::Tool::Select   },
            { "line",     QStringLiteral("直线"),            CanvasScene::Tool::Line     },
            { "rect",     QStringLiteral("矩形"),            CanvasScene::Tool::Rect     },
            { "ellipse",  QStringLiteral("椭圆"),            CanvasScene::Tool::Ellipse  },
            { "triangle", QStringLiteral("三角形"),          CanvasScene::Tool::Triangle },
            { "bezier",   QStringLiteral("贝塞尔曲线"),      CanvasScene::Tool::Bezier   },
            { "pencil",   QStringLiteral("铅笔"),            CanvasScene::Tool::Pencil   },
            { "eraser",   QStringLiteral("橡皮擦"),          CanvasScene::Tool::Eraser   },
        };

        auto* grid = new QGridLayout();
        grid->setSpacing(4);
        grid->setContentsMargins(0, 0, 0, 0);

        m_toolGroup = new QButtonGroup(panel);
        m_toolGroup->setExclusive(true);

        int i = 0;
        for (const ToolDef& t : tools)
        {
            auto* btn = new QToolButton(panel);
            btn->setObjectName(QStringLiteral("Tool"));
            btn->setIcon(iconOf(t.icon));
            btn->setIconSize(QSize(24, 24));
            btn->setToolTip(t.tip);
            btn->setCheckable(true);
            btn->setFixedSize(42, 42);

            m_toolGroup->addButton(btn, int(t.tool));
            grid->addWidget(btn, i / 3, i % 3);
            ++i;
        }

        connect(m_toolGroup, &QButtonGroup::idClicked, this, [this](int id) { selectTool(id); });
        v->addLayout(grid);

        v->addWidget(makeSeparator(panel));

        // —— 颜色 ——
        v->addWidget(makeSectionTitle(QStringLiteral("颜色"), panel));

        auto* colorRow = new QHBoxLayout();
        colorRow->setSpacing(4);
        colorRow->setContentsMargins(0, 0, 0, 0);

        const unsigned palette[] = { 0x000000, 0xDC3C3C, 0xF09600, 0x3CAA3C, 0x1E78DC, 0x8C50C8 };

        m_colorGroup = new QButtonGroup(panel);
        m_colorGroup->setExclusive(true);

        int ci = 0;
        for (unsigned c : palette)
        {
            auto* btn = new QToolButton(panel);
            btn->setObjectName(QStringLiteral("Swatch"));
            btn->setIcon(swatchIcon(c));
            btn->setIconSize(QSize(20, 20));
            btn->setCheckable(true);
            btn->setFixedSize(27, 27);
            btn->setToolTip(QStringLiteral("#%1").arg(c, 6, 16, QLatin1Char('0')).toUpper());

            m_colorGroup->addButton(btn, ci++);
            colorRow->addWidget(btn);
        }
        colorRow->addStretch();

        connect(m_colorGroup, &QButtonGroup::idClicked, this, [this, palette](int id)
        {
            m_scene->setPenColor(palette[id]);
        });

        v->addLayout(colorRow);

#ifdef PAINTQT_HAVE_OPENCV
        // —— 位图（需要 OpenCV）——
        v->addWidget(makeSeparator(panel));
        v->addWidget(makeSectionTitle(QStringLiteral("位图"), panel));

        v->addWidget(makeActionButton(panel, "image", QStringLiteral("打开位图"),
                                      QStringLiteral("把一张图片放到画布上"),
                                      [this] { openBitmap(); }));

        v->addWidget(makeActionButton(panel, "gray", QStringLiteral("转灰度"),
                                      QStringLiteral("把选中的位图转成灰度"),
                                      [this] { applyBitmapFilter(0); }));

        v->addWidget(makeActionButton(panel, "edge", QStringLiteral("边缘检测"),
                                      QStringLiteral("对选中的位图做 Canny 边缘检测"),
                                      [this] { applyBitmapFilter(1); }));

        v->addWidget(makeActionButton(panel, "wand", QStringLiteral("魔棒抠图"),
                                      QStringLiteral("点一下位图上要保留的区域，其余变透明"),
                                      [this] { startWand(); }));

        v->addWidget(makeActionButton(panel, "reset", QStringLiteral("还原原图"),
                                      QStringLiteral("把位图恢复成刚打开时的样子"),
                                      [this] { resetBitmap(); }));
#endif

        v->addWidget(makeSeparator(panel));

        // —— 操作 ——
        v->addWidget(makeSectionTitle(QStringLiteral("操作"), panel));

        v->addWidget(makeActionButton(panel, "undo", QStringLiteral("撤销"),
                                      QStringLiteral("撤销上一个图元（Ctrl+Z）"),
                                      [this] { m_scene->undo(); }));

        v->addWidget(makeActionButton(panel, "trash", QStringLiteral("清空"),
                                      QStringLiteral("清空整个画布"),
                                      [this] { m_scene->clearAll(); }));

        v->addWidget(makeActionButton(panel, "import", QStringLiteral("导入 SVG"),
                                      QStringLiteral("把 SVG 矢量图解析成可编辑的图形"),
                                      [this] { importSvg(); }));

        v->addWidget(makeActionButton(panel, "save", QStringLiteral("保存 PNG"),
                                      QStringLiteral("把画布导出成 PNG 图片（Ctrl+S）"),
                                      [this] { savePng(); }));

        v->addStretch();

        // —— 提示 ——
        m_hint = new QLabel(panel);
        m_hint->setObjectName(QStringLiteral("Hint"));
        m_hint->setWordWrap(true);
        v->addWidget(m_hint);

        // 默认选中第一个颜色
        if (QAbstractButton* b = m_colorGroup->button(0))
            b->setChecked(true);

        return panel;
    }

    QToolButton* makeActionButton(QWidget* parent, const char* icon, const QString& text,
                                  const QString& tip, std::function<void()> onClick)
    {
        auto* btn = new QToolButton(parent);
        btn->setObjectName(QStringLiteral("Action"));
        btn->setIcon(iconOf(icon));
        btn->setIconSize(QSize(18, 18));
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        connect(btn, &QToolButton::clicked, this, [onClick] { onClick(); });
        return btn;
    }

    void addShortcut(const QKeySequence& key, std::function<void()> fn)
    {
        auto* act = new QAction(this);
        act->setShortcut(key);
        connect(act, &QAction::triggered, this, [fn] { fn(); });
        addAction(act);
    }

    // ------------------------------ 工具切换 ------------------------------

    void selectTool(int id)
    {
        const auto tool = CanvasScene::Tool(id);

        m_scene->setTool(tool);
        m_view->setSelectMode(tool == CanvasScene::Tool::Select);   // 只有选择工具开框选

        // 魔棒不在工具按钮组里，所以可能取不到按钮（返回 nullptr 就跳过）
        if (QAbstractButton* b = m_toolGroup->button(id))
            b->setChecked(true);

        m_hint->setText(hintFor(tool));
    }

    // ------------------------------ 位图功能 ------------------------------

#ifdef PAINTQT_HAVE_OPENCV

    void openBitmap()
    {
        const QString file = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("打开位图"),
            QString(),
            QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff);;所有文件 (*)"));

        if (file.isEmpty())
            return;

        openBitmapFile(file);
    }

    // which: 0 = 灰度, 1 = 边缘检测
    void applyBitmapFilter(int which)
    {
        BitmapItem* bmp = m_scene->currentBitmap();
        if (!bmp)
        {
            statusBar()->showMessage(QStringLiteral("请先打开一张位图"), 4000);
            return;
        }

        // 注意：滤镜都基于**原图**算，所以连着点几次不会一层层叠加变糊
        const QImage result = (which == 0)
            ? bitmap::toGray(bmp->source())
            : bitmap::edgeDetect(bmp->source(), 50, 150);

        bmp->applyFilter(result);

        statusBar()->showMessage(
            (which == 0) ? QStringLiteral("已转灰度（基于原图计算，可点「还原原图」回到初始状态）")
                         : QStringLiteral("已做边缘检测（基于原图计算，可点「还原原图」回到初始状态）"),
            6000);
    }

    void startWand()
    {
        if (!m_scene->currentBitmap())
        {
            statusBar()->showMessage(QStringLiteral("请先打开一张位图"), 4000);
            return;
        }

        selectTool(int(CanvasScene::Tool::Wand));
        statusBar()->showMessage(
            QStringLiteral("魔棒：在位图上点一下要**保留**的区域，其余会变透明"), 8000);
    }

    void resetBitmap()
    {
        BitmapItem* bmp = m_scene->currentBitmap();
        if (!bmp)
        {
            statusBar()->showMessage(QStringLiteral("请先打开一张位图"), 4000);
            return;
        }

        bmp->resetToSource();
        statusBar()->showMessage(QStringLiteral("已还原为原图"), 4000);
    }

#endif // PAINTQT_HAVE_OPENCV

    // ------------------------------ 文件操作 ------------------------------

    void importSvg()
    {
        const QString file = QFileDialog::getOpenFileName(
            this,
            QStringLiteral("选择 SVG 文件"),
            QString(),
            QStringLiteral("SVG 矢量图 (*.svg);;所有文件 (*)"));

        if (!file.isEmpty())
            importFile(file);
    }

    void savePng()
    {
        const QString file = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("保存为 PNG"),
            QStringLiteral("画图板.png"),
            QStringLiteral("PNG 图片 (*.png)"));

        if (file.isEmpty())
            return;

        // 只导出有内容的范围，四周留 20 像素边距
        QRectF area = m_scene->itemsBoundingRect();
        area = area.isEmpty() ? m_scene->sceneRect() : area.adjusted(-20, -20, 20, 20);

        QImage img(int(area.width()), int(area.height()), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::white);

        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        m_scene->render(&p, QRectF(img.rect()), area);
        p.end();

        if (img.save(file))
            statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(file), 6000);
        else
            QMessageBox::warning(this, QStringLiteral("保存失败"),
                                 QStringLiteral("无法写入文件：\n%1").arg(file));
    }

    // ------------------------------ 成员 ------------------------------

    CanvasScene*  m_scene = nullptr;
    CanvasView*   m_view = nullptr;
    QButtonGroup* m_toolGroup = nullptr;
    QButtonGroup* m_colorGroup = nullptr;
    QLabel*       m_hint = nullptr;
    QLabel*       m_countLabel = nullptr;
    QLabel*       m_zoomLabel = nullptr;

#ifdef PAINTQT_HAVE_OPENCV
    int m_wandTolerance = 40;       // 魔棒颜色容差
#endif
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PaintQt"));

    MainWindow w;
    w.show();
    w.fitPage();        // 启动时先让整张画布可见

    // 命令行可以带一个文件：.svg 当矢量导入，其他当位图打开
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1)
    {
        const QString path = args[1];
        if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
            w.importFile(path);
        else
            w.openBitmapFile(path);
    }

    return app.exec();
}
