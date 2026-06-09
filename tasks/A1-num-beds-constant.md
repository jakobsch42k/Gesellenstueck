# A1: NUM_BEDS constant

**Severity:** Medium | **Effort:** S | **Depends on:** — (do before A2) | **Status:** open

## Problem

The bed count `5` is a bare literal in at least 12 locations: `src/sensors.cpp` (16, 60, 116), `src/irrigation.cpp` (44), `src/actuators.cpp` (4, 32, 106), `src/fileManager.cpp` (38, 64–66), `src/webBackend.cpp` (98–101, 184, 389), `src/shared.h` (12–13 array sizes). Changing the bed count means hunting every literal; missing one is a silent buffer/loop mismatch.

## Fix design

1. `src/constants.h`:
   ```cpp
   constexpr uint8_t NUM_BEDS = 5;
   ```
2. Replace array dimensions in `shared.h` (`soilPerc[NUM_BEDS]`, `moisture[NUM_BEDS]`, `ERR_SOIL[NUM_BEDS]`, …) and every loop bound / size check in the files above.
3. **Careful sites, not mechanical:**
   - `webBackend.cpp` validation that checks moisture array size and `valve_open` range 1–5 → `1..NUM_BEDS`.
   - `fileManager.cpp` JSON (de)serialization loops for moisture/calibration arrays.
   - Frontend (`data/script.js`) hardcodes 5 in many places too — out of scope here (the UI grid layout is also 5-specific); note in commit that NUM_BEDS is firmware-only for now.
4. Line numbers above are from audit at commit `ad228c4` — re-grep before editing: `grep -n "\b5\b" src/*.cpp src/*.h` and judge each hit (not every 5 is the bed count: PWM values, timeouts, pin numbers must NOT be replaced).

## Files

- `src/constants.h`, `src/shared.h`, `src/sensors.cpp`, `src/irrigation.cpp`, `src/actuators.cpp`, `src/fileManager.cpp`, `src/webBackend.cpp`

## Verification

1. `pio run` — zero warnings about array bounds.
2. Behavior must be bit-identical (pure rename): flash and confirm 5 beds render, valves 1–5 respond, config round-trips.
3. `grep -rn "NUM_BEDS" src/` shows it used everywhere bed-count semantics apply; remaining literal 5s in src/ each have non-bed meaning.
