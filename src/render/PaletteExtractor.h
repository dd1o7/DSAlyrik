#pragma once

#include <QColor>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <optional>

class QNetworkReply;

struct Palette {
    QColor text; // lyric
    QColor highlight;
    QColor secondary;
};

// Derives a restrained highlight/secondary pair from album art.
// Artwork is read from file:// or data: URLs, or fetched over http(s) (size- and time-capped),
// held in memory only, and never written to disk.
class PaletteExtractor : public QObject {
    Q_OBJECT
public:
    explicit PaletteExtractor(QObject *parent = nullptr);

    // nullopt when the image is null or has no clear color (e.g. grayscale art).
    static std::optional<Palette> fromImage(const QImage &image);

    void request(const QUrl &artUrl); // replaces any pending request
    void cancel();
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }

    static constexpr qint64 kMaxArtBytes = 4 * 1024 * 1024;
    static constexpr int kMaxArtworkSide = 512;

signals:
    void paletteReady(const QUrl &artUrl, const Palette &palette);
    void artworkReady(const QUrl &artUrl, const QImage &image); // downscaled, in memory only
    void paletteUnavailable(const QUrl &artUrl);

private:
    void finish(const QUrl &url, const QImage &image);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    quint64 m_generation = 0;
    int m_timeoutMs = 5000;
};
