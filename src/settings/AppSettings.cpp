#include "settings/AppSettings.h"

#include "playback/MprisPlayer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <cmath>

#include "lyrics/TrackNameNormalizer.h"

const QColor AppSettings::kDefaultText(0xe0, 0x40, 0x2e);      // coral-red words
const QColor AppSettings::kDefaultHighlight(0xf0, 0x40, 0x70); // rose block and symbols
const QColor AppSettings::kDefaultSecondary(0x6e, 0x24, 0x24); // the oldest, darkest words

namespace {

const QString kStyle = QStringLiteral("overlay/style");
const QString kVisible = QStringLiteral("overlay/visible");
const QString kKeepAwake = QStringLiteral("power/keepDisplayAwake");
const QString kBackdrop = QStringLiteral("overlay/backdrop");
const QString kArtBackdrop = QStringLiteral("overlay/artBackdrop");
const QString kContinuousDrift = QStringLiteral("overlay/continuousDrift");
const QString kIdleDelay = QStringLiteral("overlay/screensaverIdleSec");
const QString kHideFullscreen = QStringLiteral("overlay/hideOverFullscreen");
const QString kDisabledDisplays = QStringLiteral("overlay/disabledDisplays");
const QString kFont = QStringLiteral("text/font");
const QString kScale = QStringLiteral("text/scale");
const QString kOffset = QStringLiteral("sync/offsetMs");
const QString kText = QStringLiteral("colors/text");
const QString kHighlight = QStringLiteral("colors/highlight");
const QString kSecondary = QStringLiteral("colors/secondary");
const QString kAutoSync = QStringLiteral("colors/autoSync");
const QString kPlayer = QStringLiteral("playback/player");

QColor readColor(const QSettings &store, const QString &key, const QColor &fallback)
{
    const QColor c = QColor::fromString(store.value(key).toString());
    return c.isValid() ? c : fallback;
}

bool readBool(const QSettings &store, const QString &key, bool fallback)
{
    const QString v = store.value(key).toString();
    return v == u"true" ? true : v == u"false" ? false : fallback;
}

} // namespace

namespace {

// The app was called Wordlight during development; carry its settings over once.
std::unique_ptr<QSettings> defaultStore()
{
    auto store = std::make_unique<QSettings>(QStringLiteral("dsalyrik"), QStringLiteral("dsalyrik"));
    if (store->allKeys().isEmpty()) {
        const QSettings previous(QStringLiteral("wordlight"), QStringLiteral("wordlight"));
        for (const QString &key : previous.allKeys())
            store->setValue(key, previous.value(key));
    }
    return store;
}

} // namespace

AppSettings::AppSettings(std::unique_ptr<QSettings> store, const QString &keyFile, QObject *parent)
    : QObject(parent), m_store(store ? std::move(store) : defaultStore()),
      m_keyFile(!keyFile.isEmpty() ? keyFile
                                   : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                         + QStringLiteral("/dsalyrik/spicylyrics.key"))
{
    load();
}

void AppSettings::load()
{
    bool ok = false;
    const int style = m_store->value(kStyle).toInt(&ok);
    if (ok && style >= int(OverlayWindow::Style::Drift) && style <= int(OverlayWindow::Style::Karaoke))
        m_style = OverlayWindow::Style(style);
    m_text = readColor(*m_store, kText, kDefaultText);
    m_highlight = readColor(*m_store, kHighlight, kDefaultHighlight);
    m_secondary = readColor(*m_store, kSecondary, kDefaultSecondary);
    m_autoSync = readBool(*m_store, kAutoSync, true);
    m_overlayVisible = readBool(*m_store, kVisible, true);
    m_keepAwake = readBool(*m_store, kKeepAwake, true);
    m_backdrop = readBool(*m_store, kBackdrop, true);
    m_artBackdrop = readBool(*m_store, kArtBackdrop, true);
    m_continuousDrift = readBool(*m_store, kContinuousDrift, false);
    bool idleOk = false;
    const int idle = m_store->value(kIdleDelay).toInt(&idleOk);
    m_idleDelaySec = idleOk ? std::clamp(idle, 0, 3600) : 0;
    m_hideOverFullscreen = readBool(*m_store, kHideFullscreen, true);
    m_disabledDisplays = m_store->value(kDisabledDisplays).toStringList();
    m_disabledDisplays.removeAll(QString());
    m_disabledDisplays.removeDuplicates();
    m_fontFamily = m_store->value(kFont).toString().trimmed();
    bool scaleOk = false;
    const double scale = m_store->value(kScale).toDouble(&scaleOk);
    m_textScale = scaleOk ? std::clamp(scale, kMinTextScale, kMaxTextScale) : 1.0;
    bool offsetOk = false;
    const int offset = m_store->value(kOffset).toInt(&offsetOk);
    m_timingOffsetMs = offsetOk ? std::clamp(offset, -kMaxTimingOffsetMs, kMaxTimingOffsetMs) : 0;
    const QString player = m_store->value(kPlayer).toString();
    m_preferredPlayer = player.startsWith(Mpris::kServicePrefix) ? player : QString();
}

void AppSettings::setStyle(OverlayWindow::Style style)
{
    if (style == m_style)
        return;
    m_style = style;
    m_store->setValue(kStyle, int(style));
    emit styleChanged(style);
}

void AppSettings::setColors(const QColor &text, const QColor &highlight, const QColor &secondary)
{
    if (!text.isValid() || !highlight.isValid() || !secondary.isValid())
        return;
    if (text == m_text && highlight == m_highlight && secondary == m_secondary)
        return;
    m_text = text;
    m_highlight = highlight;
    m_secondary = secondary;
    m_store->setValue(kText, text.name(QColor::HexRgb));
    m_store->setValue(kHighlight, highlight.name(QColor::HexRgb));
    m_store->setValue(kSecondary, secondary.name(QColor::HexRgb));
    emit colorsChanged();
}

void AppSettings::setAutoSync(bool enabled)
{
    if (enabled == m_autoSync)
        return;
    m_autoSync = enabled;
    m_store->setValue(kAutoSync, enabled);
    emit autoSyncChanged(enabled);
}

void AppSettings::setOverlayVisible(bool visible)
{
    if (visible == m_overlayVisible)
        return;
    m_overlayVisible = visible;
    m_store->setValue(kVisible, visible);
    emit overlayVisibleChanged(visible);
}

void AppSettings::setKeepAwake(bool enabled)
{
    if (enabled == m_keepAwake)
        return;
    m_keepAwake = enabled;
    m_store->setValue(kKeepAwake, enabled);
    emit keepAwakeChanged(enabled);
}

void AppSettings::setHideOverFullscreen(bool enabled)
{
    if (enabled == m_hideOverFullscreen)
        return;
    m_hideOverFullscreen = enabled;
    m_store->setValue(kHideFullscreen, enabled);
    emit hideOverFullscreenChanged(enabled);
}

void AppSettings::setIdleDelaySec(int seconds)
{
    const int clamped = std::clamp(seconds, 0, 3600);
    if (clamped == m_idleDelaySec)
        return;
    m_idleDelaySec = clamped;
    m_store->setValue(kIdleDelay, clamped);
    emit idleDelayChanged(clamped);
}

void AppSettings::setContinuousDrift(bool enabled)
{
    if (enabled == m_continuousDrift)
        return;
    m_continuousDrift = enabled;
    m_store->setValue(kContinuousDrift, enabled);
    emit continuousDriftChanged(enabled);
}

void AppSettings::setArtBackdrop(bool enabled)
{
    if (enabled == m_artBackdrop)
        return;
    m_artBackdrop = enabled;
    m_store->setValue(kArtBackdrop, enabled);
    emit artBackdropChanged(enabled);
}

QString AppSettings::spicyLyricsKey() const
{
    if (const QString env = qEnvironmentVariable("SPICY_LYRICS_SECRET_KEY").trimmed(); !env.isEmpty())
        return env;
    QFile file(m_keyFile);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void AppSettings::setSpicyLyricsKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    if (trimmed.isEmpty()) {
        QFile::remove(m_keyFile);
        return;
    }
    QDir().mkpath(QFileInfo(m_keyFile).absolutePath());
    QFile file(m_keyFile);
    // Owner-only before any secret is written.
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(trimmed.toUtf8());
}

void AppSettings::setBackdrop(bool enabled)
{
    if (enabled == m_backdrop)
        return;
    m_backdrop = enabled;
    m_store->setValue(kBackdrop, enabled);
    emit backdropChanged(enabled);
}

void AppSettings::setDisplayEnabled(const QString &screenName, bool enabled)
{
    if (screenName.isEmpty() || enabled == isDisplayEnabled(screenName))
        return;
    if (enabled)
        m_disabledDisplays.removeAll(screenName);
    else
        m_disabledDisplays.append(screenName);
    m_store->setValue(kDisabledDisplays, m_disabledDisplays);
    emit disabledDisplaysChanged();
}

void AppSettings::setFontFamily(const QString &family)
{
    const QString trimmed = family.trimmed();
    if (trimmed == m_fontFamily)
        return;
    m_fontFamily = trimmed;
    m_store->setValue(kFont, trimmed);
    emit textAppearanceChanged();
}

void AppSettings::setTextScale(double scale)
{
    const double clamped = std::round(std::clamp(scale, kMinTextScale, kMaxTextScale) * 100) / 100;
    if (qFuzzyCompare(clamped, m_textScale))
        return;
    m_textScale = clamped;
    m_store->setValue(kScale, clamped);
    emit textAppearanceChanged();
}

namespace {
const QString kTrackGroup = QStringLiteral("timing/tracks/");
const QString kPlayerGroup = QStringLiteral("timing/players/");
} // namespace

int AppSettings::trackOffsetMs(const QString &trackKey) const
{
    return trackKey.isEmpty() ? 0 : m_store->value(kTrackGroup + trackKey, 0).toInt();
}

int AppSettings::playerOffsetMs(const QString &playerId) const
{
    return playerId.isEmpty() ? 0 : m_store->value(kPlayerGroup + playerId, 0).toInt();
}

int AppSettings::effectiveOffsetMs(const QString &trackKey, const QString &playerId) const
{
    const int total = m_timingOffsetMs + playerOffsetMs(playerId) + trackOffsetMs(trackKey);
    return std::clamp(total, -kMaxTimingOffsetMs * 3 / 2, kMaxTimingOffsetMs * 3 / 2);
}

QString AppSettings::trackKey(const QString &title, const QString &artist)
{
    using N = TrackNameNormalizer;
    const QString name = N::comparable(N::cleanTitle(title));
    if (name.isEmpty())
        return {};
    // Hashed so the settings file never lists what was played.
    const QByteArray id = (name + u'|' + N::comparable(N::primaryArtist(artist))).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex().left(16));
}

QString AppSettings::playerId(const QString &service)
{
    return service.mid(Mpris::kServicePrefix.size()).section(u'.', 0, 0).toLower();
}

void AppSettings::setTrackOffsetMs(const QString &trackKey, int offsetMs)
{
    if (trackKey.isEmpty())
        return;
    const int clamped = std::clamp(offsetMs, -kMaxTimingOffsetMs, kMaxTimingOffsetMs);
    if (clamped == trackOffsetMs(trackKey))
        return;
    if (clamped == 0)
        m_store->remove(kTrackGroup + trackKey);
    else
        m_store->setValue(kTrackGroup + trackKey, clamped);
    emit scopedTimingChanged();
}

void AppSettings::setPlayerOffsetMs(const QString &playerId, int offsetMs)
{
    if (playerId.isEmpty())
        return;
    const int clamped = std::clamp(offsetMs, -kMaxTimingOffsetMs, kMaxTimingOffsetMs);
    if (clamped == playerOffsetMs(playerId))
        return;
    if (clamped == 0)
        m_store->remove(kPlayerGroup + playerId);
    else
        m_store->setValue(kPlayerGroup + playerId, clamped);
    emit scopedTimingChanged();
}

void AppSettings::setTimingOffsetMs(int offsetMs)
{
    const int clamped = std::clamp(offsetMs, -kMaxTimingOffsetMs, kMaxTimingOffsetMs);
    if (clamped == m_timingOffsetMs)
        return;
    m_timingOffsetMs = clamped;
    m_store->setValue(kOffset, clamped);
    emit timingOffsetChanged(clamped);
}

void AppSettings::setPreferredPlayer(const QString &service)
{
    if (service == m_preferredPlayer || !(service.isEmpty() || service.startsWith(Mpris::kServicePrefix)))
        return;
    m_preferredPlayer = service;
    m_store->setValue(kPlayer, service);
    emit preferredPlayerChanged(service);
}
