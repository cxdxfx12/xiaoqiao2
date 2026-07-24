#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QLinearGradient>
#include <QPainterPath>
#include <QFile>
#include <QDir>
#include <QFont>
#include <QDebug>
#include <QProcess>

struct SizedPng { int px; const char *name; };

static void DrawLogoSchemeB(QPainter &p, int px) {
    const qreal R = (qreal)px;

    // 1) 圆角方形：蓝→青渐变
    QLinearGradient bg(0, 0, R, R);
    bg.setColorAt(0.0, QColor("#4facfe"));
    bg.setColorAt(1.0, QColor("#00c6fb"));
    QPainterPath bgp;
    bgp.addRoundedRect(QRectF(0,0,R,R).adjusted(1,1,-1,-1), R*0.21, R*0.21);
    p.fillPath(bgp, bg);

    // 2) 快递盒（居中比例56%，高56%，左上起点(22%,30%)
    qreal bx = R * 0.22;
    qreal by = R * 0.30;
    qreal bw = R * 0.56;
    qreal bh = R * 0.56;
    QPointF p0(bx + bw/2, by);             // 顶部盖尖
    QPointF p1(bx,        by + bh*0.18);    // 左顶角
    QPointF p2(bx,        by + bh*0.82);    // 左底
    QPointF p3(bx + bw/2, by + bh);         // 底中
    QPointF p4(bx + bw,   by + bh*0.82);    // 右底
    QPointF p5(bx + bw,   by + bh*0.18);    // 右顶
    QPointF lidMid(bx + bw/2, by + bh*0.36); // 盒盖横向折痕
    QPointF tapeTopL(bx + bw * 0.33, by + bh*0.04);
    QPointF tapeTopR(bx + bw * 0.67, by + bh*0.04);

    QPen penBox(QColor("#0d5cb8"), qMax<qreal>(1.0, R*0.016));
    penBox.setJoinStyle(Qt::RoundJoin);
    penBox.setCapStyle(Qt::RoundCap);

    QPainterPath box;
    box.moveTo(p0); box.lineTo(p1); box.lineTo(p2);
    box.lineTo(p3); box.lineTo(p4); box.lineTo(p5);
    box.closeSubpath();
    p.fillPath(box, Qt::white);
    p.strokePath(box, penBox);

    QPainterPath lines;
    lines.moveTo(p0); lines.lineTo(p3);      // 中心竖
    lines.moveTo(p1); lines.lineTo(p5);      // 盒盖横线（上1/6
    lines.moveTo(p1); lines.lineTo(lidMid);   // 左折 → 中心
    lines.lineTo(p5);
    p.strokePath(lines, penBox);

    // 3) 黄色封箱胶带（略微倾斜，贴在盖子顶部
    qreal tapeH = bh*0.28;
    qreal tapeW = bw * 0.34;
    qreal tapeCx = bx + bw*0.5;
    qreal tapeCy = by + bh*0.13;
    QRectF tapeRect0(-tapeW/2, -tapeH/2, tapeW, tapeH);
    p.save();
    p.translate(tapeCx, tapeCy);
    p.rotate(-8.0);
    QPainterPath tapePath;
    qreal rr = qMin<qreal>(R*0.04, tapeH*0.3);
    tapePath.addRoundedRect(tapeRect0, rr, rr);
    p.fillPath(tapePath, QColor("#ffd86b"));
    QPen penTape(QColor("#e6a23c"), qMax<qreal>(0.8, R*0.008));
    p.strokePath(tapePath, penTape);
    p.restore();

    // 4) 盒面中心蓝色"乔"字贴标
    QRectF label(bx + bw*0.22, by + bh*0.52, bw*0.56, bh*0.32);
    QPainterPath lp;
    qreal lrr = qMin<qreal>(R*0.055, label.height()*0.35);
    lp.addRoundedRect(label, lrr, lrr);
    p.fillPath(lp, QColor("#409eff"));
    p.setPen(Qt::NoPen);
    int fontPx = (int)(label.height() * 0.74);
    QFont font(QStringLiteral("PingFang SC"), fontPx, QFont::Black);
    font.setStyleStrategy(QFont::PreferAntialias);
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(label, Qt::AlignCenter, QStringLiteral("乔"));

    // 5) 左上角祥云（白色柔半透明）
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255,255,255,200));
    qreal cx = R * 0.18, cy = R * 0.155, rad = R * 0.088;
    QPainterPath cloud;
    cloud.addEllipse(QPointF(cx, cy), rad*1.00, rad*0.82);
    cloud.addEllipse(QPointF(cx + rad*0.85, cy + rad*0.22), rad*0.70, rad*0.58);
    cloud.addEllipse(QPointF(cx - rad*0.75, cy + rad*0.18), rad*0.74, rad*0.60);
    p.drawPath(cloud);

    // 6) 右下绿色小算盘（绿色渐变+框+上2下1珠
    qreal ax = R * 0.68;
    qreal ay = R * 0.70;
    qreal aw = R * 0.24;
    qreal ah = R * 0.22;
    QLinearGradient abG(0, ay, 0, ay + ah);
    abG.setColorAt(0.0, QColor("#43e97b"));
    abG.setColorAt(1.0, QColor("#38f9d7"));
    QPainterPath ap;
    qreal arr = qMin<qreal>(R*0.04, ah*0.3);
    ap.addRoundedRect(QRectF(ax,ay,aw,ah), arr, arr);
    p.fillPath(ap, abG);
    QPen abPen(QColor("#0a7a5a"), qMax<qreal>(0.8, R*0.01));
    p.setBrush(Qt::NoBrush);
    p.strokePath(ap, abPen);
    p.setPen(abPen);
    p.drawLine(QPointF(ax, ay + ah*0.36), QPointF(ax+aw, ay+ah*0.36));
    p.drawLine(QPointF(ax, ay + ah*0.66), QPointF(ax+aw, ay+ah*0.66));
    p.setBrush(QColor("#0a7a5a"));
    p.setPen(Qt::NoPen);
    qreal r1 = qMax<qreal>(1.2, R*0.022);
    p.drawEllipse(QPointF(ax + aw*0.25, ay + ah*0.20), r1, r1);
    p.drawEllipse(QPointF(ax + aw*0.50, ay + ah*0.20), r1, r1);
    p.drawEllipse(QPointF(ax + aw*0.75, ay + ah*0.50), r1, r1);

    // 7) 顶部柔光高光，让图标更立体
    p.setClipping(true);
    QPainterPath clipR;
    clipR.addRoundedRect(QRectF(0,0,R,R).adjusted(2,2,-2,-2), R*0.21, R*0.21);
    p.setClipPath(clipR);
    QLinearGradient hg(0, 0, 0, R*0.6);
    hg.setColorAt(0.0, QColor(255,255,255,58));
    hg.setColorAt(1.0, QColor(255,255,255,0));
    p.fillRect(QRectF(0, 0, R, R), hg);
    p.setClipping(false);
}

int main(int argc, char **argv) {
    QGuiApplication a(argc, argv);
    QString outDir = (argc > 1) ? QString::fromUtf8(argv[1])
                                 : QLatin1String("xiaoqiao.iconset");
    QDir().mkpath(outDir);
    QDir().mkpath(outDir + "/../png");

    SizedPng spec[] = {
        {16,   "icon_16x16.png"},
        {32,   "icon_16x16@2x.png"},
        {32,   "icon_32x32.png"},
        {64,   "icon_32x32@2x.png"},
        {128,  "icon_128x128.png"},
        {256,  "icon_128x128@2x.png"},
        {256,  "icon_256x256.png"},
        {512,  "icon_256x256@2x.png"},
        {512,  "icon_512x512.png"},
        {1024, "icon_512x512@2x.png"}
    };
    const int N = sizeof(spec)/sizeof(spec[0]);
    for (int i = 0; i < N; ++i) {
        const auto &s = spec[i];
        QImage img(s.px, s.px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        DrawLogoSchemeB(p, s.px);
        p.end();
        QString f1 = outDir + "/" + s.name;
        img.save(f1, "PNG");
        qDebug() << "png ->" << f1;
    }
    // 额外导出 1024 / 256 / 128 给资源文件 / Windows exe 用
    for (int px : {1024, 512, 256, 128, 64, 48, 32, 24, 16}) {
        QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing
                         | QPainter::SmoothPixmapTransform);
        DrawLogoSchemeB(p, px);
        p.end();
        QString fp = outDir + QString("/../png/logo_%1.png").arg(px);
        QFileInfo(fp).dir().mkpath(".");
        img.save(fp, "PNG");
        qDebug() << "png ->" << fp;
    }

    // 8) 调用 Mac 系统 iconutil 生成 .icns
    QString iconsetPath = QFileInfo(outDir).absoluteFilePath();
    QString icnsPath = iconsetPath + ".icns"; // 默认同名.icns，
    // iconutil 要求 iconset 文件夹后缀 .iconset 且输出在同级目录
    QFileInfo iconsetInfo(iconsetPath);
    QString iconsetDir = iconsetInfo.absolutePath() + "/xiaoqiao.iconset";
    QString icnsOut  = iconsetInfo.absolutePath() + "/xiaoqiao.icns";
    if (iconsetPath != iconsetDir) {
        QProcess::execute("/bin/sh", QStringList() << "-c"
            << QString("rm -rf %1 && cp -R %2 %1").arg(
                iconsetDir.toUtf8().constData(),
                iconsetPath.toUtf8().constData()));
    }
    int rc = QProcess::execute("/usr/bin/iconutil",
                               QStringList() << "-c" << "icns"
                               << iconsetDir << "-o" << icnsOut);
    if (rc == 0 && QFile::exists(icnsOut)) {
        qDebug() << "icns ->" << icnsOut << "SIZE="
                 << QFile(icnsOut).size();
        // 也复制一份到 ../png/ 旁边
        QString dst = iconsetInfo.absolutePath() + "/../xiaoqiao.icns";
        QFile::remove(dst);
        QFile::copy(icnsOut, dst);
    } else {
        qWarning() << "iconutil failed rc=" << rc;
        return 2;
    }
    return 0;
}
