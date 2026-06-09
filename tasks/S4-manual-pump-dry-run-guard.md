# S4: Manual pump dry-run guard

**Severity:** High | **Effort:** S | **Depends on:** — | **Status:** open

## Problem

`SystemController::handleManualCommand` executes `pump_on()` unconditionally (src/systemController.cpp:115):

```cpp
if      (cmd == "pump_on")        pump_on();
```

No check of `liveData.waterCritical` or `errorFlags.ERR_WATER_CRITICAL`. Manual mode is meant to bypass *regulation*, not physics — running the pump against an empty tank risks burning the pump motor. (Automatic irrigation is already locked out by the critical-fault path; only the manual path is exposed.)

## Fix design

```cpp
if (cmd == "pump_on") {
    if (!liveData.waterCritical || errorFlags.ERR_WATER_CRITICAL) {
        Serial.println("[systemController] manual pump_on refused — water critical");
        // surface to UI: set lastErrorMessage or rely on next poll showing pump still off
    } else {
        pump_on();
    }
}
```

Note polarity: `liveData.waterCritical == true` means water is *sufficient* (documented in CLAUDE.md /data.json keys). Mirror the exact condition used by the auto-latch at src/systemController.cpp:60.

Optionally have the `/manual` HTTP handler return 409 for a refused command so the frontend can toast it — check how `_manualCtrlCb` is wired in `src/webBackend.cpp` (callback returns void today; returning a bool is a small signature change, worth it for user feedback).

## Files

- `src/systemController.cpp` (handleManualCommand)
- `src/webBackend.cpp` (optional: propagate refusal as HTTP 409)

## Verification

1. `pio run`.
2. Bench: trip the critical float (or disconnect to simulate empty), enable maintenance mode, send `pump_on` from Control tab — pump must stay off, serial logs refusal, UI pump chip stays "Idle" on next poll.
3. With water OK, `pump_on` works as before.
