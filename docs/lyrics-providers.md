# Lyric providers

Reviewed 2026-10-26 from the LRCLIB API documentation (`https://lrclib.net/docs`)
and API terms (`https://lrclib.net/docs/terms`).

## LRCLIB (first provider)

- **Endpoint used:** `GET https://lrclib.net/api/get?track_name=…&artist_name=…[&album_name=…][&duration=…]`.
  `duration` is in seconds (1–3600). A record matches only within ±2 s, so we send it whenever MPRIS supplies a length.
  `404 TrackNotFound` means no match.
- **Fields used:** `syncedLyrics` (line-timed LRC), `instrumental`, `trackName`, `artistName`.
  `plainLyrics` has no timing, so it counts as no match for a synchronized display.
- **Access:** no API key. Registration (`Lrclib-App-Id`) is optional and only raises limits; DSAlyrik does not register and stores no secret.
- **Identification (required):** `User-Agent` names the application and version. It carries the project homepage: `DSAlyrik/<version> (https://github.com/dd1o7/DSAlyrik)`.
- **Rate limits (approximate, not contractual):** ~90 requests/min per IP for an identified client.
  On `429`/`503` the client **must** honor `Retry-After`. DSAlyrik blocks further requests for that period plus up to 30 % jitter, and never retries automatically.
- **Throttling:** send requests sequentially. DSAlyrik requests at most once per track change and cancels the stale request.
- **Terms of use:** API use is permitted free of charge, anonymous access included.
  Clients must identify themselves, honor `Retry-After`, limit concurrency, and cache where appropriate.
  The permission covers the API only. Lyrics may be protected by third-party copyright, and the person using the software is responsible for their own use.
  DSAlyrik displays lyrics transiently and caches them in memory only. It never writes lyric text to disk.
- **Attribution:** no attribution is required by the terms. We credit LRCLIB in the README and third-party notices as a courtesy.

## Follow-up: word-level timing

LRCLIB records carry a `lyricsfile` YAML document. When `hasWordSync` is true it contains
measured word timings (`lines[].words`). This is covered by the same API terms.
Parsing it would replace the approximate `WordTimingSynthesizer` timing for those tracks.
It needs a YAML reader and is deferred to a later phase.
