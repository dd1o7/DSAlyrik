# DSAlyrik

Word-by-word synchronized lyrics for whatever is playing on your Linux desktop.
DSAlyrik sits in the system tray, follows any MPRIS media player (Spotify, mpv, browsers,
Elisa, VLC, …) and draws the current line on a transparent, click-through overlay.

Inspired by lock-screen lyric apps on macOS; an independent, open-source implementation for Linux.

## Features

- **Four styles:** Drift, Lens (a convex lens follows the sung word), Lens + Glyphs, and Glyphs (words paired with symbols)
- **Colors:** three colors, or Auto Sync from the album art
- **Text:** font and text size; a timing nudge if your setup reports position late or early
- **Displays:** one overlay per display, each can be switched off
- **Keep display awake while playing:** display idle only; never blocks suspend or locking
- **Lyrics:** from [LRCLIB](https://lrclib.net), held in memory only and never written to disk
- **Power:** no timers run while nothing is playing; while playing it wakes only at word boundaries

## Build

Requires Qt 6.11 (Core, DBus, Widgets, Quick, Network, Test), Qt Shader Tools (`qsb`), CMake ≥ 3.25 and a C++20 compiler.
On Fedora:

```bash
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtshadertools qt6-qtsvg
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build          # unit and integration tests (needs dbus-daemon)
./build/dsalyrik
```

A Flatpak manifest is in `packaging/flatpak/`.

## Using it

Start DSAlyrik and play music. The tray menu controls style, colors, text, timing, displays, player selection and keep-awake.
Clicking the tray icon shows or hides the lyrics.

Development options: `--preview <0-3>` plays synthetic lyrics in a style, `--watch-playback` / `--watch-lyrics` print what DSAlyrik sees,
and `--lookup <artist> <title> [seconds]` tests a lyric lookup.

## Desktop notes

- **KDE Plasma (Wayland):** built with LayerShellQt (`layer-shell-qt-devel`), the lyrics are a native overlay-layer
  surface: above windows, never focused, click-through, sharp under fractional scaling. Built without it (or started
  with `--xwayland`), the overlay runs through XWayland, where KWin honours keep-above and click-through for X11 windows.
- **GNOME:** the tray needs an AppIndicator/StatusNotifierItem extension. Without a tray, the overlay stays visible
  and DSAlyrik explains how to quit.
- **X11 sessions:** expected to work, but not yet validated.
- **Lock screen:** lyrics are not shown on the lock screen (the macOS app does this through private APIs; there is no
  portable Linux equivalent).
- **Apple Music:** there is no Linux app. A browser session works only if the browser publishes the track via MPRIS.

## Troubleshooting

- *No lyrics:* LRCLIB may not have synced lyrics for the track. The overlay says so. `--lookup` shows what the service returns.
- *Lyrics feel early or late:* use Timing → Show lyrics earlier/later in the tray.
- *Wrong player followed:* choose it under Player in the tray.
- *Display stays awake after a crash (KDE):* KDE's portal keeps an idle inhibition after the requesting process dies.
  DSAlyrik releases it on normal exit and on Ctrl+C/SIGTERM. After a crash, run
  `systemctl --user restart plasma-xdg-desktop-portal-kde.service`.

## Credits and license

MIT; see `LICENSE`. Lyrics are provided by LRCLIB under its API terms and remain the property of their rights holders;
see `THIRD_PARTY_NOTICES.md` and `docs/lyrics-providers.md`.
