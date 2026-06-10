# A3: Magic numbers → constants.h / Config

**Severity:** Medium | **Effort:** M | **Depends on:** ideally A2 (touches same files) | **Status:** done

## Problem

Tunables and addresses are scattered outside `constants.h`/`pins.h`/`Config` (audit refs at commit `ad228c4`):

| Value | Location | Should live in |
|---|---|---|
| `Ki = 0.3f` (light integrator gain) | lightManagement.cpp:6 | constants.h |
| `LIGHT_RAMP_INTERVAL_MS = 20`, `LIGHT_RAMP_STEP_MAX = 2` | lightManagement.cpp:9–10 (#define in .cpp) | constants.h |
| Roof open PWM 120, close PWM 70, pump PWM 200 | actuators.h:8–9 default args | **Config** (user-tunable, see below) |
| `BLINK_INTERVAL_MS = 500` | actuators.cpp:8 | constants.h |
| LCD I2C address `0x27` | display.cpp:6 | constants.h (it's a bus address, not a pin) |
| BME280 addresses `0x76`/`0x77` | sensors.cpp:29–31 | constants.h |
| WiFi channel `6` | webBackend.cpp:515 | constants.h |
| `"/plants.tmp"` path | fileManager.cpp:196 | constants.h (only path not already there) |

## Fix design

Two tiers:

**Tier 1 — constants.h moves (pure relocation, no behavior change):** everything except motor/pump PWM. Mechanical; keep names, add a `// hardware addresses` section grouping.

**Tier 2 — motor/pump PWM into Config (behavior-affecting feature):**
1. Add `uint8_t roofOpenPwm, roofClosePwm, pumpPwm` to `Config` (src/shared.h) with defaults 120/70/200 in constants.h.
2. **CONFIG_VERSION bump required** + the 4-location sync documented at shared.h:27–28 (struct, defaults, fileManager (de)serialize, webBackend validate/apply). Warning: current `jsonToConfig` treats version mismatch as hard reset to defaults — users lose settings on this update. Either accept (note in commit/release msg) or do the config-migration feature first (deferred tier — see tasks/README.md).
3. Validation bounds in `validateConfig`: roofOpenPwm 60–255, roofClosePwm 40–255, pumpPwm 100–255 (floor values keep mechanics moving; asymmetric open/close defaults are intentional — mechanical advantage, see CLAUDE.md).
4. Actuator call sites pass `cfg.roofOpenPwm` etc. instead of relying on default args; remove the default args from the signatures so the compiler finds every call site.
5. Optional UI: three steppers in a collapsed "Advanced" section of the Beds/System tab — or skip UI and expose via `/saveConfig` only (curl-tunable). Decide at implementation time.

## Files

- `src/constants.h`, `src/shared.h`, `src/lightManagement.cpp`, `src/actuators.{h,cpp}`, `src/display.cpp`, `src/sensors.cpp`, `src/webBackend.cpp`, `src/fileManager.cpp`
- Optional: `data/index.html`, `data/script.js` (advanced steppers)

## Verification

1. Tier 1: `pio run`; flash; behavior identical (pure relocation).
2. Tier 2: `pio run`; flash; confirm config migration behavior (settings reset is expected if no migration — verify defaults land); set pumpPwm to 150 via `/saveConfig`, confirm pump audibly slower and value persists across reboot; out-of-range values → 400.
3. Frontend/backend contract diff if UI steppers are added (CLAUDE.md rule).
