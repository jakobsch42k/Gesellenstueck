# S8: Irrigation pause lower bound

**Severity:** Medium | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`irrigationPause_ms` has a max bound but no minimum in `validateConfig` (src/webBackend.cpp:~136; `constants.h` defines `IRRIGATE_PAUSE_MS_MAX` but no `*_MIN`). A user (or a buggy client) can set the pause to 0 via `/saveConfig` or `/importConfig`. The irrigation FSM (src/irrigation.cpp) then cycles IDLE → PUMPING (10 s) → PAUSING (0 ms) → IDLE, and because soil does not absorb water instantly, the same first-dry bed re-triggers immediately: a continuous pump loop that floods the bed and runs the pump near-continuously.

The pause exists precisely to let water diffuse before re-measuring — it is the only re-irrigation guard in the FSM.

## Fix design

1. `src/constants.h`: `#define IRRIGATE_PAUSE_MS_MIN 60000UL` (1 min floor; default is 10 min).
2. `validateConfig` (src/webBackend.cpp): reject `irrigationPause_ms < IRRIGATE_PAUSE_MS_MIN` with the same error-message pattern as existing bounds checks. Apply the same review to `irrigationDuration_ms` (sane min, e.g. 1000 ms, and confirm a max exists).
3. `fileManager` load path: clamp out-of-range persisted values to defaults on load (config written by older firmware or hand-edited could carry 0).

## Files

- `src/constants.h`
- `src/webBackend.cpp` (validateConfig)
- `src/fileManager.cpp` (load-time clamp)

## Verification

1. `pio run`.
2. `curl -X POST http://192.168.4.1/saveConfig -d '{"irrigationPause_ms": 0}'` → 400 with bounds message.
3. Valid value (e.g. 120000) accepted and persists across reboot.
