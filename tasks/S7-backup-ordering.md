# S7: Fix config backup ordering + compact serialization

**Severity:** High | **Effort:** S | **Depends on:** — | **Status:** open

## Problem

`saveConfig` (src/fileManager.cpp) calls `backupConfig()` at line 145 *before* writing the temp file at line 151. `backupConfig()` (line 214–224) ends with `LittleFS.rename(CONFIG_PATH, CONFIG_BAK1_PATH)` — config.json is *moved away*. If the subsequent `LittleFS.open(CONFIG_TMP_PATH, "w")` fails (line 151) or the write fails, there is no `/config.json` anymore; only bak1 holds the last good config, and the boot path expects CONFIG_PATH. Narrow window, but it is a data-loss ordering bug in code whose whole purpose is power-loss safety.

Secondary: both `saveConfig` (line 156) and `savePlants` (line 202) use `serializeJsonPretty` — ~2.5× the bytes of compact JSON written to flash on every save, for files no human reads in place.

## Fix design

Reorder `saveConfig`:

1. Write new config to `CONFIG_TMP_PATH`, close, verify (non-zero size; optionally re-parse).
2. *Then* `backupConfig()` (rotates bak3←bak2←bak1←config.json).
3. `LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH)`.

Failure analysis after reorder: tmp write fails → config.json untouched, abort, return false. Rotation fails midway → config.json may be at bak1 but tmp is verified good, rename still lands it at CONFIG_PATH. Power loss between rotate and rename → boot finds no config.json, falls back to defaults — *unless* the loader already tries bak1; check `loadConfig`'s fallback chain and, if it doesn't try backups, add bak1 as fallback (cheap, closes the last window).

Replace `serializeJsonPretty` → `serializeJson` at fileManager.cpp:156 and 202. Loader uses ArduinoJson `deserializeJson`, which is whitespace-agnostic — no compatibility issue with existing pretty files.

## Files

- `src/fileManager.cpp` (saveConfig ordering, both serialize calls, optionally loadConfig bak1 fallback)

## Verification

1. `pio run`.
2. Flash, change a setting in the UI, reboot — config persists (round-trip through new ordering).
3. Inspect serial log for the save sequence; confirm backups rotate (`/config.bak1` exists after second save).
4. Confirm compact files load: old pretty config.json from before the change must still parse (it will — same parser).
