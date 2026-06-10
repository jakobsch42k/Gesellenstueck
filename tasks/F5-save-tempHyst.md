# F5: Send tempHyst on save

**Severity:** Medium | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`saveConfig` in `data/script.js` (~500–505) POSTs `{ moisture, luxTarget, tempTarget, lightProfile }`. `tempHyst` is read from the firmware on load (`c.tempHysteresis`, script.js:~394) and used to render the "opens above / closes below" band, but is never sent back. Today the UI has no hysteresis editor, so nothing breaks actively — but the payload is an incomplete representation of what the UI claims to manage, and any future firmware-side default change silently desynchronizes the displayed band from saved intent.

## Fix design

Frontend/backend contract diff first (per CLAUDE.md rule):

1. List keys backend `validateConfig`/`applyJsonToConfig` accepts (src/webBackend.cpp:95–170) vs. keys `saveConfig` sends. Confirm the backend key name — likely `tempHysteresis` (the GET emits `tempHysteresis`; script reads that). `/saveConfig` accepts partial JSON, so adding the key is backward-compatible.
2. Add to the POST body: `tempHysteresis: config.tempHyst`.
3. While in the contract diff: verify every other key the UI displays-and-saves round-trips by exact name (moisture array vs moisture1..5 alias handling — see F2).

Optional (separate decision, not required): add a small hysteresis stepper to the Climate card so the value is actually editable. If skipped, the save is still correct — it persists what was loaded.

## Files

- `data/script.js` (saveConfig body ~500–505)
- `src/webBackend.cpp` (read-only: confirm key acceptance; no change expected)

## Verification

1. `node --check data/script.js`.
2. Contract diff documented in the commit message (backend keys vs frontend keys, per CLAUDE.md).
3. Save from UI, then `GET /config` — `tempHysteresis` unchanged/echoed correctly; 400 must not occur.
4. `pio run --target uploadfs`.
