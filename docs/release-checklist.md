# Release checklist

## Before any public release
- [x] Project namespace: app ID `io.github.dd1o7.DSAlyrik`.
- [x] Homepage `https://github.com/dd1o7/DSAlyrik` in the LRCLIB User-Agent and metainfo.
- [ ] Create that GitHub repository so the homepage and bug-tracker links resolve.
- [ ] Build and run the Flatpak (`flatpak-builder` was not available during development).
- [ ] CI green on the hosted runner.

## Manual pass (KDE Plasma Wayland)
- [ ] Player connect/disconnect, switching between two players, preferred player
- [ ] Track change, seek, pause/resume; lyrics stay in sync (adjust Timing if needed)
- [ ] All four styles; text size and font; colors and Auto Sync with Spotify (https art) and mpv (data: art)
- [ ] Missing lyrics and missing artwork states
- [ ] Overlay stays on top and is click-through; one overlay per display; display toggles
- [ ] Keep awake: the display stays on while playing and idles normally after pause/quit
- [ ] Tray unavailable (e.g. GNOME without extension): overlay visible, quit instructions printed
- [ ] Quit from the tray and with Ctrl+C: no leftover idle inhibition
- [ ] Clean install from the Flatpak; desktop entry and icon appear
