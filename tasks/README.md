# Upgrade Tasks

Individual, self-contained tasks from the 2026-06-09 code-logic audit (commit `ad228c4`).
Each file can be executed in its own session — it carries the full problem evidence, fix design, and verification steps.

## Recommended order

1. **S1–S8** (safety-critical firmware) — small, independently flashable and verifiable
2. **F1–F6** (frontend logic) — independent of firmware, any time
3. **A1–A3** (architecture refactor) — **last**, because A2 rewrites the modules the S-fixes touch; the fixes carry over into the refactored classes

## Index

| ID | Task | Severity | Effort | Depends on | Done |
|----|------|----------|--------|------------|------|
| [S1](S1-hardware-watchdog.md) | Hardware watchdog | Critical | S | — | ☑ |
| [S2](S2-fs-fault-actuator-stop.md) | Stop actuators on FS fault | Critical | S | — | ☐ |
| [S3](S3-bme-staleness-guard.md) | BME280 staleness guard | High | M | — | ☐ |
| [S4](S4-manual-pump-dry-run-guard.md) | Manual pump dry-run guard | High | S | — | ☐ |
| [S5](S5-led-pwm-clamp.md) | Clamp manual LED PWM | High | S | — | ☐ |
| [S6](S6-roof-min-dwell.md) | Roof min-dwell + reed abort | High | M | — | ☐ |
| [S7](S7-backup-ordering.md) | Fix config backup ordering | High | S | — | ☐ |
| [S8](S8-irrigation-pause-min-bound.md) | Irrigation pause lower bound | Medium | S | — | ☐ |
| [F1](F1-await-before-toggle.md) | Await POST before UI toggle | Critical (UI) | M | — | ☐ |
| [F2](F2-falsy-zero-config-guards.md) | Falsy-zero config guards | High | S | — | ☐ |
| [F3](F3-avgseries-partial-data.md) | avgSeries with partial data | High | S | — | ☐ |
| [F4](F4-poll-backoff-visibility.md) | Poll backoff + visibility pause | Medium | M | — | ☐ |
| [F5](F5-save-tempHyst.md) | Send tempHyst on save | Medium | S | — | ☐ |
| [F6](F6-localstorage-versioning.md) | localStorage schema version | Medium | S | — | ☐ |
| [A1](A1-num-beds-constant.md) | NUM_BEDS constant | Medium | S | — | ☐ |
| [A2](A2-oop-module-classes.md) | OOP module classes | Medium | L | A1, S1–S8 | ☐ |
| [A3](A3-magic-numbers-to-config.md) | Magic numbers → constants/Config | Medium | M | A2 (ideally) | ☐ |

## Deferred (audited, no task file — user deselected feature tier)

- Web OTA firmware update endpoint (currently USB-only)
- Config schema migration (new Config field currently wipes user config on version bump)
- `POST /restoreBackup` endpoint (backend `restoreBackup()` exists in fileManager.cpp but unexposed)
- mDNS (`greenhouse.local`), SSE/async web server, NTP/RTC time source
- AP password (`src/constants.h:7`) — intentionally simple during testing phase; owner updates it himself later

## Conventions

- Build check: `pio run` (firmware) · `node --check data/script.js` (frontend) · `pio run --target uploadfs` after `data/` changes
- Control-logic tasks (S3, S6) must walk the failure-mode checklist from project CLAUDE.md: stale data, oscillation, no-regulation
- One task per commit, conventional commit format
