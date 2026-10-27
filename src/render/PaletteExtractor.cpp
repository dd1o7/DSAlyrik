#include "render/PaletteExtractor.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QNetworkReply>
#include <QTimer>
#include <array>

#include "lyrics/LrcLibProvider.h"

namespace {

constexpr int kHueBuckets = 24;
constexpr double kMinChromaShare = 0.05; // below this, the art counts as grayscale

QImage readCapped(QIODevice *device)
{
    QImageReader reader(device);
    reader.setAllocationLimit(64); // MB; rejects decompression bombs
    return reader.read();
}

} // namespace

PaletteExtractor::PaletteExtractor(QObject *parent) : QObject(parent) {}

std::optional<Palette> PaletteExtractor::fromImage(const QImage &image)
{
    if (image.isNull())
        return std::nullopt;
    const QImage small = image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_RGB32);

    struct Bucket { double weight = 0, r = 0, g = 0, b = 0; };
    std::array<Bucket, kHueBuckets> buckets{};
    double chroma = 0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = small.pixelColor(x, y);
            const double s = c.hsvSaturationF(), v = c.valueF();
            if (s < 0.25 || v < 0.2)
                continue; // grays, near-black and washed-out pixels carry no hue
            const double w = s * v;
            Bucket &bucket = buckets[std::min(kHueBuckets - 1, c.hsvHue() * kHueBuckets / 360)];
            bucket.weight += w;
            bucket.r += c.redF() * w;
            bucket.g += c.greenF() * w;
            bucket.b += c.blueF() * w;
            chroma += w;
        }
    }
    if (chroma / (small.width() * small.height()) < kMinChromaShare)
        return std::nullopt;

    const Bucket &top = *std::max_element(buckets.cbegin(), buckets.cend(),
                                          [](const Bucket &a, const Bucket &b) { return a.weight < b.weight; });
    const QColor dominant = QColor::fromRgbF(top.r / top.weight, top.g / top.weight, top.b / top.weight);
    const double hue = std::max(0.0, double(dominant.hsvHueF()));
    // Restrained: bright enough to read on any wallpaper, never fully saturated.
    const double s = std::clamp(double(dominant.hsvSaturationF()), 0.45, 0.82);
    return Palette{QColor::fromHsvF(hue, s * 0.6, 0.97),                          // lyric: light tint
                   QColor::fromHsvF(hue, s, 0.88),                                // highlight: saturated
                   QColor::fromHsvF(hue, std::clamp(s * 0.45, 0.15, 0.4), 0.68)}; // secondary: muted
}

void PaletteExtractor::request(const QUrl &artUrl)
{
    cancel();
    const quint64 generation = m_generation;

    if (artUrl.isLocalFile()) {
        QTimer::singleShot(0, this, [this, artUrl, generation] {
            if (generation != m_generation)
                return;
            QFile file(artUrl.toLocalFile());
            const bool readable = QFileInfo(file).size() <= kMaxArtBytes && file.open(QIODevice::ReadOnly);
            finish(artUrl, readable ? readCapped(&file) : QImage());
        });
        return;
    }
    if (artUrl.scheme() == u"data") {
        QTimer::singleShot(0, this, [this, artUrl, generation] {
            if (generation != m_generation)
                return;
            const QByteArray encoded = artUrl.toEncoded();
            const qsizetype comma = encoded.indexOf(',');
            QByteArray bytes = comma >= 0 && encoded.left(comma).endsWith(";base64")
                                   ? QByteArray::fromBase64(QByteArray::fromPercentEncoding(encoded.mid(comma + 1)))
                                   : QByteArray();
            if (bytes.size() > kMaxArtBytes)
                bytes.clear();
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::ReadOnly);
            finish(artUrl, readCapped(&buffer));
        });
        return;
    }
    if (artUrl.scheme() != u"http" && artUrl.scheme() != u"https") {
        QTimer::singleShot(0, this, [this, artUrl, generation] {
            if (generation == m_generation)
                emit paletteUnavailable(artUrl);
        });
        return;
    }

    QNetworkRequest request(artUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, LrcLibProvider::userAgent());
    request.setTransferTimeout(m_timeoutMs);
    m_reply = m_network.get(request);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > kMaxArtBytes || total > kMaxArtBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, artUrl, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            emit paletteUnavailable(artUrl);
            return;
        }
        QByteArray data = reply->readAll();
        QBuffer buffer(&data);
        buffer.open(QIODevice::ReadOnly);
        finish(artUrl, readCapped(&buffer));
    });
}

void PaletteExtractor::cancel()
{
    ++m_generation;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

void PaletteExtractor::finish(const QUrl &url, const QImage &image)
{
    if (!image.isNull())
        emit artworkReady(url, image.width() > kMaxArtworkSide || image.height() > kMaxArtworkSide
                                   ? image.scaled(kMaxArtworkSide, kMaxArtworkSide, Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation)
                                   : image);
    if (const auto palette = fromImage(image))
        emit paletteReady(url, *palette);
    else
        emit paletteUnavailable(url);
}
