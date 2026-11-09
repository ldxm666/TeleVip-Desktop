# Changelog

[简体中文](CHANGELOG.md) · **English**

## v2026.11.10.1 · Stable

- Fix a main-thread hang caused by non-progressing neighbour traversal during deleted-message restoration.
- Publish interception state and request a label refresh immediately; batch archive writes and durable flushes on a background worker.
- Show pending persistence accurately and retry failed writes with backoff.
- Bound restoration work by item count and elapsed time to reduce long UI stalls.
- Read archive pages asynchronously, coalesce rapid paging, discard stale results, and stop periodic reads while the page is hidden.
- Pin module versions to sessions so existing clients keep renewing and updated clients receive the repair, without extending card expiry.
- Keep desktop changes and new tests MIT-licensed; the repaired Hook implementation remains private.

After upgrading, restart Telegram and inject again with the updated client. An already loaded module is not replaced in a running process.

Validation: 173 core unit tests, 8 binary-adaptation audit tests, 10 native host test groups, 21 backend regressions, and a real signed activation/download/renewal probe passed. The hang was sampled from a live process and covered by hidden-neighbour and blocked-disk regression fixtures. Multi-day live-chat endurance has not yet been completed.

## v2026.11.07.1 · Stable

- Publish the desktop UI, motion, translation tools, licensing client and tests under MIT; retain the core Hook privately.
- Add a buildable UI preview, bilingual build guide and explicit source boundary.
- Fix valid long-duration cards being treated as invalid after short-session or network failures.
- Retry session renewal, reauthenticate expired short tokens and restore preserved feature switches after recovery.
- Preserve same-device concurrent sessions, device binding and the original card expiry; keep server resource limits unchanged.

## v2026.11.07 · Stable

- Published as a stable release and marked as the latest GitHub version.
- Updated Chinese and English download links, distribution terms and bundled instructions.
- Uses the verified client binary; no feature changes or expanded compatibility claims in this release.

## v2026.11.07-beta.1

- Initial closed-source public beta for Windows x64.
- Unified WhiteSur-inspired activation screen, custom window controls and light / dark themes.
- Improved entry, exit, hover, press, focus and status transitions, including reduced-motion support.
- Square donation / Telegram buttons and a fix for the reproduced hover repaint conflict.
- Online licensing, device binding, signed-module verification and file-hash checks.
- Free daily license keys through the Telegram check-in bot.
- English and Simplified Chinese GitHub documentation, release notes and feedback forms.

Verification covers native host tests, UI regressions, activation and module checks. Injection into the target Telegram process was not rerun in this round. No stress-test or universal frame-rate claim is made.
