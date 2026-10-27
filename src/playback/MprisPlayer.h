#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

enum class PlaybackState { Stopped, Paused, Playing };

struct TrackSnapshot {
    QString service;
    QString trackId;
    QString title;
    QString artist;
    QString album;
    qint64 lengthMs = 0; // 0 when the player omits mpris:length
    qint64 positionMs = 0;
    PlaybackState state = PlaybackState::Stopped;
    QUrl artUrl;
    QUrl url; // xesam:url: the playing file or stream, when reported

    bool isValid() const { return !service.isEmpty(); }
};

// Interpolates playback position between D-Bus reads. Times are monotonic ms.
struct PositionClock {
    qint64 basePositionMs = 0;
    qint64 baseTimeMs = 0;
    double rate = 1.0;
    bool running = false;

    void reset(qint64 positionMs, qint64 nowMs, bool playing, double playbackRate = 1.0);
    qint64 positionAt(qint64 nowMs, qint64 lengthMs) const;
};

namespace Mpris {
inline const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
inline const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
inline const QString kServicePrefix = QStringLiteral("org.mpris.MediaPlayer2.");

qint64 monotonicNowMs();
PlaybackState parseStatus(const QString &status);
// Replaces the metadata fields of a snapshot; missing keys leave them empty.
void applyMetadata(TrackSnapshot &snapshot, const QVariantMap &metadata);
QVariantMap toVariantMap(const QVariant &value); // unwraps nested QDBusArgument maps
} // namespace Mpris

// One MPRIS player service. Reads properties asynchronously and keeps an
// interpolated position between reads.
class MprisPlayer : public QObject {
    Q_OBJECT
public:
    MprisPlayer(const QString &service, const QDBusConnection &bus, QObject *parent = nullptr);

    QString service() const { return m_track.service; }
    TrackSnapshot snapshot() const;
    qint64 lastChangedMs() const { return m_lastChangedMs; }
    void refresh();
    // Re-reads only Position; corrects the interpolated clock if it drifted.
    void resyncPosition();
    static constexpr qint64 kDriftToleranceMs = 40;

signals:
    void changed();

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changedProps, const QStringList &invalidated);
    void onSeeked(qlonglong positionUs);

private:
    void applyProperties(const QVariantMap &props);

    QDBusConnection m_bus;
    TrackSnapshot m_track;
    PositionClock m_clock;
    qint64 m_lastChangedMs = 0;
};
