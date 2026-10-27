# Contributing

- Build and run the tests before sending changes: `cmake -S . -B build && cmake --build build && ctest --test-dir build`.
- New behaviour needs a test that fails first. D-Bus tests run on a private bus (`tests/dbus-test-session.conf`)
  with fake services; render tests run offscreen with the software renderer and fail on any QML warning.
- Keep the power rule: no timers or animations while nothing is playing, and no looping animations.
- Never write lyric text or listening history to disk, and never add service secrets.
- Do not copy code, assets, names or copy from any other product. Behaviour notes belong in `docs/`,
  marked as observed or inferred.
- Explain notable design decisions in the commit message or the pull request.
