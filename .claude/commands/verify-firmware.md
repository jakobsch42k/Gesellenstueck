---
description: Autonomous firmware+UI verification loop — build, uploadfs, Playwright-check the web UI, and refuse to mark complete until every step passes green.
---

# Verify Firmware

A hard gate. Do NOT claim success, mark a task done, or commit until ALL four steps pass with evidence. If any step fails, stop, report the failure with its output, fix, and re-run the whole loop from step 1.

## 1. Build firmware + stage frontend
- Run `~/.platformio/penv/Scripts/pio.exe run`.
- Confirm `[SUCCESS]`. The build's `gzip_assets` hook stages `data/` → `.gz` twins; confirm the staging line lists the changed assets.
- Fail → report compiler/linker output, stop.

## 2. Upload LittleFS assets
- Run `~/.platformio/penv/Scripts/pio.exe run -t uploadfs`.
- Confirm `[SUCCESS]` and `Hash of data verified`.
- If serving logic changed, also re-flash firmware (`-t upload`) — gz-aware firmware needs the gz FS and vice versa (see CLAUDE.md Known Issues).
- Fail (e.g. `COM port busy`) → report, stop. Port held by a serial monitor = close it and retry.

## 3. Playwright UI check
- **Precondition:** the dev machine must be joined to the ESP32 WiFi AP; the UI is served at `http://192.168.4.1` (offline AP, no internet). If not reachable, report that the device isn't connected — do NOT mark verified.
- Navigate to `http://192.168.4.1`, assert:
  - `<h1>` / dashboard root is visible (page actually rendered, not garbled symbols → catches double Content-Encoding bug).
  - Response header `content-encoding` appears exactly once for each `.gz`-served asset (no duplicate gzip → the double-inflate garbage bug from CLAUDE.md).
  - No console errors; `/data.json` poll returns 200 with expected keys.
- Fail → capture screenshot + failing assertion, stop.

## 4. Gate
- Only if steps 1–3 are all green: state the claim WITH the evidence inline (build SUCCESS, uploadfs verified, UI assertions passed).
- Then — and only then — stage specific files, commit with a conventional message, and push.
