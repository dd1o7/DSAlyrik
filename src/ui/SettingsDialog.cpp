#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "settings/AppSettings.h"
#include "ui/TrayController.h"

namespace {

QCheckBox *checkBox(const QString &text, const char *name, QWidget *parent)
{
    auto *box = new QCheckBox(text, parent);
    box->setObjectName(QString::fromLatin1(name));
    return box;
}

QSpinBox *offsetBox(const char *name, QWidget *parent)
{
    auto *box = new QSpinBox(parent);
    box->setObjectName(QString::fromLatin1(name));
    box->setRange(-AppSettings::kMaxTimingOffsetMs, AppSettings::kMaxTimingOffsetMs);
    box->setSingleStep(50);
    box->setSuffix(QStringLiteral(" ms"));
    return box;
}

void paintSwatch(QPushButton *button, const QColor &color)
{
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; min-width: 44px; min-height: 22px; "
                                         "border-radius: 4px; border: 1px solid palette(mid); }")
                              .arg(color.name()));
}

} // namespace

SettingsDialog::SettingsDialog(AppSettings *settings, QWidget *parent) : QDialog(parent), m_settings(settings)
{
    setWindowTitle(tr("DSAlyrik Settings"));
    setWindowIcon(TrayController::appIcon());
    auto *tabs = new QTabWidget(this);
    AppSettings *s = m_settings;

    // Two-way binding for a checkbox.
    const auto bindCheck = [this](QCheckBox *box, auto getter, auto setter, auto changed) {
        box->setChecked((m_settings->*getter)());
        connect(box, &QCheckBox::toggled, m_settings, setter);
        connect(m_settings, changed, box, [this, box, getter] {
            const QSignalBlocker block(box);
            box->setChecked((m_settings->*getter)());
        });
    };

    // ---- Look ----
    auto *look = new QWidget;
    auto *lookForm = new QFormLayout(look);
    auto *style = new QComboBox;
    style->setObjectName(QStringLiteral("style"));
    for (auto st : {OverlayWindow::Style::Drift, OverlayWindow::Style::Lens, OverlayWindow::Style::LensGlyphs,
                    OverlayWindow::Style::Glyphs, OverlayWindow::Style::Karaoke})
        style->addItem(OverlayWindow::styleName(st));
    style->setCurrentIndex(int(s->style()));
    connect(style, &QComboBox::currentIndexChanged, s, [s](int i) { s->setStyle(OverlayWindow::Style(i)); });
    connect(s, &AppSettings::styleChanged, style, [style](OverlayWindow::Style st) {
        const QSignalBlocker block(style);
        style->setCurrentIndex(int(st));
    });
    lookForm->addRow(tr("Style"), style);

    auto *fontRow = new QHBoxLayout;
    auto *fontButton = new QPushButton;
    fontButton->setObjectName(QStringLiteral("fontButton"));
    auto *defaultFont = new QPushButton(tr("Default (Nunito)"));
    const auto showFont = [s, fontButton] {
        fontButton->setText(s->fontFamily().isEmpty() ? QStringLiteral("Nunito") : s->fontFamily());
    };
    showFont();
    connect(fontButton, &QPushButton::clicked, this, [this, s] {
        bool ok = false;
        QFont current;
        current.setFamily(s->fontFamily().isEmpty() ? QStringLiteral("Nunito") : s->fontFamily());
        const QFont picked = QFontDialog::getFont(&ok, current, this, tr("Lyric font"));
        if (ok)
            s->setFontFamily(picked.family());
    });
    connect(defaultFont, &QPushButton::clicked, s, [s] { s->setFontFamily({}); });
    fontRow->addWidget(fontButton, 1);
    fontRow->addWidget(defaultFont);
    lookForm->addRow(tr("Font"), fontRow);

    auto *sizeRow = new QHBoxLayout;
    auto *size = new QSlider(Qt::Horizontal);
    size->setObjectName(QStringLiteral("textScale"));
    size->setRange(int(AppSettings::kMinTextScale * 100), int(AppSettings::kMaxTextScale * 100));
    size->setSingleStep(5);
    auto *sizeLabel = new QLabel;
    const auto showSize = [s, size, sizeLabel] {
        const QSignalBlocker block(size);
        size->setValue(qRound(s->textScale() * 100));
        sizeLabel->setText(QStringLiteral("%1 %").arg(qRound(s->textScale() * 100)));
    };
    showSize();
    connect(size, &QSlider::valueChanged, s, [s](int v) { s->setTextScale(v / 100.0); });
    connect(s, &AppSettings::textAppearanceChanged, this, [showSize, showFont] {
        showSize();
        showFont();
    });
    sizeRow->addWidget(size, 1);
    sizeRow->addWidget(sizeLabel);
    lookForm->addRow(tr("Text size"), sizeRow);

    auto *colorRow = new QHBoxLayout;
    QPushButton *swatches[3];
    const char *swatchNames[3] = {"colorText", "colorHighlight", "colorSecondary"};
    const QString swatchTips[3] = {tr("Lyric text"), tr("Highlight"), tr("Secondary text")};
    for (int i = 0; i < 3; ++i) {
        swatches[i] = new QPushButton;
        swatches[i]->setObjectName(QString::fromLatin1(swatchNames[i]));
        swatches[i]->setToolTip(swatchTips[i]);
        colorRow->addWidget(swatches[i]);
        connect(swatches[i], &QPushButton::clicked, this, [this, s, i] {
            QColor colors[] = {s->textColor(), s->highlightColor(), s->secondaryColor()};
            const QColor picked = QColorDialog::getColor(colors[i], this, tr("Choose a lyric color"));
            if (!picked.isValid())
                return;
            colors[i] = picked;
            s->setColors(colors[0], colors[1], colors[2]);
        });
    }
    auto *resetColors = new QPushButton(tr("Reset"));
    connect(resetColors, &QPushButton::clicked, s, [s] {
        s->setColors(AppSettings::kDefaultText, AppSettings::kDefaultHighlight, AppSettings::kDefaultSecondary);
    });
    colorRow->addWidget(resetColors);
    colorRow->addStretch();
    const auto showColors = [s, swatches] {
        paintSwatch(swatches[0], s->textColor());
        paintSwatch(swatches[1], s->highlightColor());
        paintSwatch(swatches[2], s->secondaryColor());
    };
    showColors();
    connect(s, &AppSettings::colorsChanged, this, showColors);
    lookForm->addRow(tr("Colors"), colorRow);

    auto *autoSync = checkBox(tr("Auto Sync colors from album art"), "autoSync", look);
    bindCheck(autoSync, &AppSettings::autoSync, &AppSettings::setAutoSync, &AppSettings::autoSyncChanged);
    lookForm->addRow(QString(), autoSync);
    auto *artBackdrop = checkBox(tr("Album art backdrop"), "artBackdrop", look);
    bindCheck(artBackdrop, &AppSettings::artBackdrop, &AppSettings::setArtBackdrop, &AppSettings::artBackdropChanged);
    lookForm->addRow(tr("Backdrop"), artBackdrop);
    auto *backdrop = checkBox(tr("Soft dark glow"), "backdrop", look);
    bindCheck(backdrop, &AppSettings::backdrop, &AppSettings::setBackdrop, &AppSettings::backdropChanged);
    lookForm->addRow(QString(), backdrop);
    auto *drift = checkBox(tr("Keep Drift moving (uses more power)"), "continuousDrift", look);
    bindCheck(drift, &AppSettings::continuousDrift, &AppSettings::setContinuousDrift,
              &AppSettings::continuousDriftChanged);
    lookForm->addRow(tr("Motion"), drift);
    tabs->addTab(look, tr("Look"));

    // ---- Behaviour ----
    auto *behaviour = new QWidget;
    auto *behaviourForm = new QFormLayout(behaviour);
    auto *show = checkBox(tr("Show lyrics"), "showLyrics", behaviour);
    bindCheck(show, &AppSettings::overlayVisible, &AppSettings::setOverlayVisible, &AppSettings::overlayVisibleChanged);
    behaviourForm->addRow(QString(), show);
    auto *saver = new QComboBox;
    saver->setObjectName(QStringLiteral("screensaver"));
    for (const auto &[label, seconds] : QList<std::pair<QString, int>>{{tr("Off: show while playing"), 0},
                                                                      {tr("After 30 s idle"), 30},
                                                                      {tr("After 1 min idle"), 60},
                                                                      {tr("After 2 min idle"), 120},
                                                                      {tr("After 5 min idle"), 300}})
        saver->addItem(label, seconds);
    const auto showSaver = [s, saver] {
        const QSignalBlocker block(saver);
        const int index = saver->findData(s->idleDelaySec());
        saver->setCurrentIndex(index >= 0 ? index : 0);
    };
    showSaver();
    connect(saver, &QComboBox::currentIndexChanged, s, [s, saver](int i) { s->setIdleDelaySec(saver->itemData(i).toInt()); });
    connect(s, &AppSettings::idleDelayChanged, this, showSaver);
    behaviourForm->addRow(tr("Screensaver mode"), saver);
    auto *fullscreen = checkBox(tr("Hide over fullscreen apps"), "hideFullscreen", behaviour);
    bindCheck(fullscreen, &AppSettings::hideOverFullscreen, &AppSettings::setHideOverFullscreen,
              &AppSettings::hideOverFullscreenChanged);
    behaviourForm->addRow(QString(), fullscreen);
    auto *awake = checkBox(tr("Keep display awake while playing"), "keepAwake", behaviour);
    bindCheck(awake, &AppSettings::keepAwake, &AppSettings::setKeepAwake, &AppSettings::keepAwakeChanged);
    behaviourForm->addRow(QString(), awake);
    auto *player = new QComboBox;
    player->setObjectName(QStringLiteral("player"));
    behaviourForm->addRow(tr("Player"), player);
    connect(player, &QComboBox::activated, s, [s, player](int i) { s->setPreferredPlayer(player->itemData(i).toString()); });
    tabs->addTab(behaviour, tr("Behaviour"));

    // ---- Timing ----
    auto *timing = new QWidget;
    auto *timingForm = new QFormLayout(timing);
    auto *all = offsetBox("offsetAll", timing);
    all->setValue(s->timingOffsetMs());
    connect(all, &QSpinBox::valueChanged, s, &AppSettings::setTimingOffsetMs);
    connect(s, &AppSettings::timingOffsetChanged, all, [all](int ms) {
        const QSignalBlocker block(all);
        all->setValue(ms);
    });
    timingForm->addRow(tr("All songs"), all);
    auto *playerOffset = offsetBox("offsetPlayer", timing);
    auto *songOffset = offsetBox("offsetSong", timing);
    timingForm->addRow(tr("This player"), playerOffset);
    timingForm->addRow(tr("This song"), songOffset);
    auto *hint = new QLabel(tr("Positive values show lyrics later, negative earlier. They add up."));
    hint->setWordWrap(true);
    timingForm->addRow(QString(), hint);
    tabs->addTab(timing, tr("Timing"));

    // ---- Lyrics sources ----
    auto *sources = new QWidget;
    auto *sourcesForm = new QFormLayout(sources);
    auto *intro = new QLabel(tr("Word-by-word timing for Spotify comes from <a href=\"https://developers.spicylyrics.org/docs\">"
                                "Spicy Lyrics</a>. Create a free application there and paste its secret key "
                                "(<code>sl_sk_…</code>). It stays on this computer, readable only by you. "
                                "Other tracks use local .lrc files and LRCLIB."));
    intro->setWordWrap(true);
    intro->setOpenExternalLinks(true);
    sourcesForm->addRow(intro);
    auto *keyRow = new QHBoxLayout;
    auto *key = new QLineEdit;
    key->setObjectName(QStringLiteral("spicyKey"));
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(QStringLiteral("sl_sk_…"));
    auto *saveKey = new QPushButton(tr("Save"));
    saveKey->setObjectName(QStringLiteral("spicyKeySave"));
    auto *removeKey = new QPushButton(tr("Remove"));
    removeKey->setObjectName(QStringLiteral("spicyKeyRemove"));
    keyRow->addWidget(key, 1);
    keyRow->addWidget(saveKey);
    keyRow->addWidget(removeKey);
    sourcesForm->addRow(tr("Spicy Lyrics key"), keyRow);
    m_keyState = new QLabel;
    sourcesForm->addRow(QString(), m_keyState);
    const auto showKeyState = [this] {
        const bool fromEnv = !qEnvironmentVariable("SPICY_LYRICS_SECRET_KEY").trimmed().isEmpty();
        m_keyState->setText(fromEnv ? tr("Using the key from $SPICY_LYRICS_SECRET_KEY.")
                                    : m_settings->spicyLyricsKey().isEmpty() ? tr("No key: Spicy Lyrics is off.")
                                                                             : tr("A key is saved."));
    };
    showKeyState();
    connect(saveKey, &QPushButton::clicked, this, [this, key, showKeyState] {
        m_settings->setSpicyLyricsKey(key->text());
        key->clear(); // a secret is never shown again
        showKeyState();
    });
    connect(removeKey, &QPushButton::clicked, this, [this, showKeyState] {
        m_settings->setSpicyLyricsKey({});
        showKeyState();
    });
    m_credits = new QLabel;
    m_credits->setObjectName(QStringLiteral("credits"));
    m_credits->setWordWrap(true);
    m_credits->setOpenExternalLinks(true);
    m_credits->setTextFormat(Qt::RichText);
    sourcesForm->addRow(tr("Now showing"), m_credits);
    tabs->addTab(sources, tr("Lyrics sources"));

    // ---- Displays ----
    auto *displays = new QWidget;
    auto *displayLayout = new QVBoxLayout(displays);
    const auto screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QString name = screens[i]->name();
        auto *box = new QCheckBox(name.isEmpty() ? tr("Display %1").arg(i + 1) : name, displays);
        box->setObjectName(QStringLiteral("display:") + name);
        box->setEnabled(!name.isEmpty());
        box->setChecked(s->isDisplayEnabled(name));
        connect(box, &QCheckBox::toggled, s, [s, name](bool on) { s->setDisplayEnabled(name, on); });
        displayLayout->addWidget(box);
    }
    displayLayout->addStretch();
    tabs->addTab(displays, tr("Displays"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    m_playerBox = player;
    m_playerOffset = playerOffset;
    m_songOffset = songOffset;
    connect(playerOffset, &QSpinBox::valueChanged, s, [this](int ms) { m_settings->setPlayerOffsetMs(m_playerId, ms); });
    connect(songOffset, &QSpinBox::valueChanged, s, [this](int ms) { m_settings->setTrackOffsetMs(m_trackKey, ms); });
    connect(s, &AppSettings::scopedTimingChanged, this, [this] { setTimingContext(m_trackKey, m_trackLabel, m_playerId, m_playerLabel); });
    setTimingContext({}, {}, {}, {});
    setPlayers({});
    setLyricsCredits({});
    resize(520, 420);
}

void SettingsDialog::setTimingContext(const QString &trackKey, const QString &trackLabel, const QString &playerId,
                                      const QString &playerLabel)
{
    m_trackKey = trackKey;
    m_trackLabel = trackLabel;
    m_playerId = playerId;
    m_playerLabel = playerLabel;
    const QSignalBlocker blockSong(m_songOffset);
    const QSignalBlocker blockPlayer(m_playerOffset);
    m_songOffset->setEnabled(!trackKey.isEmpty());
    m_songOffset->setValue(m_settings->trackOffsetMs(trackKey));
    m_songOffset->setToolTip(trackLabel);
    m_playerOffset->setEnabled(!playerId.isEmpty());
    m_playerOffset->setValue(m_settings->playerOffsetMs(playerId));
    m_playerOffset->setToolTip(playerLabel);
}

void SettingsDialog::setLyricsCredits(const LyricDocument &document)
{
    if (document.provider.isEmpty()) {
        m_credits->setText(tr("No lyrics on screen."));
        return;
    }
    QStringList parts{document.provider.toHtmlEscaped()};
    for (const LyricCredit &credit : document.credits)
        parts << (credit.url.isValid() ? QStringLiteral("<a href=\"%1\">%2</a>")
                                             .arg(credit.url.toString(QUrl::FullyEncoded).toHtmlEscaped(), credit.label.toHtmlEscaped())
                                       : credit.label.toHtmlEscaped());
    m_credits->setText(parts.join(QStringLiteral(" · ")));
}

void SettingsDialog::setPlayers(const QStringList &services)
{
    const QSignalBlocker block(m_playerBox);
    m_playerBox->clear();
    m_playerBox->addItem(tr("Automatic"), QString());
    for (const QString &service : services)
        m_playerBox->addItem(TrayController::playerLabel(service), service);
    const int index = m_playerBox->findData(m_settings->preferredPlayer());
    m_playerBox->setCurrentIndex(index >= 0 ? index : 0);
}
