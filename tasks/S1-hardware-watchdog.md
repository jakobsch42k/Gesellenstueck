# S1: Hardware watchdog

**Severity:** Critical | **Effort:** S | **Depends on:** — | **Status:** open

## Problem

No watchdog is configured anywhere in the codebase (no `esp_task_wdt_*` calls). If the main loop hangs — stuck I2C transaction in `sensors_update`, a client holding an HTTP request open against the synchronous `WebServer`, or DNS processing stall — every actuator keeps its last state forever. A running pump, an open valve, or a driving roof motor is then unattended indefinitely. This is the single highest-impact safety gap.

## Fix design

In `SystemController::init()` (src/systemController.cpp), after serial/display init:

```cpp
#include <esp_task_wdt.h>

esp_task_wdt_init(8, true);     // 8 s timeout, panic→reset on trigger
esp_task_wdt_add(NULL);         // watch the loopTask (current task)
```

In `SystemController::run()` (start of each loop pass):

```cpp
esp_task_wdt_reset();
```

Notes:
- 8 s is generous: normal loop pass is milliseconds; a LittleFS config save (20–80 ms) and slow HTTP handlers stay far under it. Do not go below ~3 s — `handleImportConfig` + backup rotation could approach 1 s worst case on a degraded filesystem.
- On Arduino-ESP32 core 3.x (espressif32 ≥ 6.x uses 2.x core where the signature above is valid; if a future platform bump moves to IDF5/core 3.x, the API becomes `esp_task_wdt_init(&(esp_task_wdt_config_t){.timeout_ms=8000,.trigger_panic=true})`). Check which one compiles; platform is pinned to `espressif32@6.12.0`.
- After watchdog reset, boot behavior is already safe: actuators init to off, maintenance mode defaults ON, roof state re-derived from reed contact.

## Files

- `src/systemController.cpp` (init + run)
- `src/constants.h` — add `#define WDT_TIMEOUT_S 8`

## Verification

1. `pio run` — must compile.
2. Flash, confirm normal operation for several minutes (no spurious resets; watch serial for `task_wdt` messages).
3. Optional fault injection: temporarily add `delay(10000)` inside `run()` guarded by a manual command, confirm panic+reset within ~8 s and clean reboot into maintenance mode. Remove injection before commit.
