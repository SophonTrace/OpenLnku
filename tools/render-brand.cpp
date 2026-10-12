#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <array>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QSvgRenderer renderer(QStringLiteral("assets/branding/mark.svg"));
    if (!renderer.isValid()) return 1;
    constexpr std::array<int, 9> sizes{16,20,24,32,40,48,64,128,256};
    std::array<QByteArray, sizes.size()> layers;
    for (size_t i = 0; i < sizes.size(); ++i) {
        QImage image(sizes[i], sizes[i], QImage::Format_ARGB32); image.fill(Qt::transparent);
        { QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); renderer.render(&painter); }
        if (sizes[i] == 256 && !image.save("assets/branding/app.png")) return 2;
        QBuffer buffer(&layers[i]); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG")) return 2;
    }
    QFile file("assets/branding/app.ico"); if (!file.open(QIODevice::WriteOnly)) return 3;
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        out << quint8(sizes[i] == 256 ? 0 : sizes[i]) << quint8(sizes[i] == 256 ? 0 : sizes[i]);
        out << quint8(0) << quint8(0) << quint16(1) << quint16(32) << quint32(layers[i].size()) << offset;
        offset += quint32(layers[i].size());
    }
    for (const auto& layer : layers) if (file.write(layer) != layer.size()) return 4;
    return out.status() == QDataStream::Ok ? 0 : 5;
}
