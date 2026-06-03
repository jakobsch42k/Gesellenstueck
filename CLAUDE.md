# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 (Freenove board) smart greenhouse controller ("Hochbeet") managing climate, irrigation, and grow lighting for a 5-bed raised garden. Runs as a WiFi access point with a web dashboard.

## Build & Flash Commands

```bash
# Build
pio run

# Flash firmware
pio run --target upload

# Upload LittleFS filesystem (web assets + config)
pio run --target uploadfs

# Serial monitor (115200 baud)
pio device monitor

# Build + upload in one step
pio run --target upload && pio device monitor
```

> **Note:** The web UI (index.html, script.js, style.css) lives in `data/` and must be uploaded separately with `uploadfs` whenever changed. Config changes made at runtime persist to LittleFS automatically.

## Architecture

### Entry Point & Orchestration

`src/main.cpp` is minimal — it instantiates `SystemController` and calls `init()` / `run()`. All logic lives in `SystemController` (`src/systemController.cpp`), which owns the main loop and coordinates all subsystems.

**Initialization order matters:**
1. Serial → Display → Actuators (all outputs safe/off first)
2. LittleFS mount + load config
3. Sensors → Control modules (roof, irrigation, light)
4. Web backend (WiFi AP starts here)
5. Maintenance mode ON by default (regulation disabled until user enables)

### Shared Data Layer

`src/shared.h` defines the three core data structures passed by pointer throughout:

- **`LiveData`** — sensor state, updated by `sensors_update()` only; all other modules read-only
- **`Config`** — persisted settings, written by `fileManager` and web API
- **`ErrorFlags`** — fault state; critical errors (`ERR_ROOF_TIMEOUT`, `ERR_WATER_CRITICAL`, `EMERGENCY_STOP`) lock all regulation; warnings allow degraded operation

`src/constants.h` holds all default values and the WiFi credentials.  
`src/pins.h` holds all GPIO assignments.

### Hardware Interface Modules

| Module | File | Notes |
|---|---|---|
| Sensors | `sensors.cpp` | BME280 (temp/humidity), BH1750 (lux), 5× soil ADC, water level switches, reed contact |
| Actuators | `actuators.cpp` | Roof motor (TB6612FNG), pump, 5 solenoid valves, grow light PWM, warning LED |
| Display | `display.cpp` | 16×2 I2C LCD at 0x27; 2 s refresh; line 1: T/H, line 2: water status |

**Sensor fault tolerance:**
- Soil ADC: plausibility check (50–4000 range); out-of-range sets `ERR_SOIL[bed]` flag
- BME280: tries addresses 0x76 then 0x77; NaN reads keep last-good value
- BH1750: 120 ms integration time; if unavailable, light controller falls back to feedforward (profile PWM only, no closed-loop correction)

**Actuator PWM defaults:** roof open = 120, roof close = 70 (asymmetric for mechanical advantage), pump = 200.

### Control Modules (FSM-based)

Each module takes `LiveData*`, `Config*`, `ErrorFlags*` in its `init()` and checks them in `update()` each loop:

| Module | File | Logic |
|---|---|---|
| Roof | `roofControl.cpp` | Temperature vs. target±hysteresis → motor open/close; reed contact gates CLOSING state; 1.5 s open pulse, 5 s close timeout |
| Irrigation | `irrigation.cpp` | Scans beds 1–5, pumps first dry bed; 10 s pump / 10 min diffusion pause cycle; `ERR_WATER_CRITICAL` immediately locks pump & valves |
| Light | `lightManagement.cpp` | Feedforward + PI controller tracking lux setpoint scaled by 24-hour hourly profile; EMA-smoothed input; slew-rate limiter (2 PWM/20 ms); anti-windup integral clamp; 5-lux dead-band |

**Safety:** `SystemController::run()` checks critical faults before calling any `_update()`. Actuator calls in error paths immediately stop motors/valves.

**Manual commands** (via `POST /manual {"cmd": "...", "val": N}`):`pump_on`, `pump_off`, `valve_open`/`valve_close` (val=1–5), `valve_closeAll`, `roof_open` (1.5 s pulse), `roof_close`, `roof_stop`, `led_pwm` (val=0–255), `emergency_stop`, `maintenance_on`, `maintenance_off`.

### Web Backend

`src/webBackend.cpp` — AsyncWebServer on port 80. Key routes:

- `GET /data` → JSON live sensor + control state (tempC, humPerc, lux, luxSmoothed, soilPerc[5], waterLow, waterCritical, roofClosed, timeOfDay, pumpStatus, all error flags, lastErrorMessage)
- `GET /systemStatus` → uptime, free heap, CPU freq, WiFi client count
- `GET /config` / `POST /saveConfig` / `POST /importConfig` → config read/write (saveConfig accepts partial JSON; importConfig replaces entire config)
- `POST /manual` → manual actuator commands
- `POST /ackErrors` → clear critical faults; also resets roof FSM to IDLE after `ERR_ROOF_TIMEOUT`

Captive portal: DNS redirects all queries to 192.168.4.1; HTTP 302s for iOS/Android/Windows probe URLs. Port 443 listener handles HTTPS probes.

Web UI assets (`data/`) are served from LittleFS. `script.js` polls `/data` on an interval and updates the dashboard. `data/plants.json` exists but is currently unused by firmware (reserved for a future plant reference UI).

### File System (LittleFS)

`src/fileManager.cpp` manages `/config.json` with 3 rolling backups (`.bak1/2/3`). Writes go to a temp file first, then atomic rename — prevents corruption on power loss. On corrupt/missing config, defaults from `constants.h` are written. A version field (`CONFIG_VERSION`) triggers defaults on schema changes.

Config exposes timing overrides: `roofOpenDuration_ms`, `roofCloseTimeout_ms`, `irrigationDuration_ms`, `irrigationPause_ms`.

## Git Workflow

After every code change, commit and push to GitHub:

```bash
git add <changed files>   # stage specific files, not git add -A
git commit -m "<message>"
git push origin main
```

**Commit message format:**

```
<type>: <short imperative summary>

<optional body: what changed and why, if not obvious>
```

Types: `feat`, `fix`, `refactor`, `chore`, `docs`

Good examples:
- `fix: clamp PI integral before slew-rate limiter to prevent overshoot`
- `feat: add per-bed irrigation duration override to config`
- `refactor: extract roof FSM timeout into named constant`

Rules:
- Subject line ≤ 72 characters, no trailing period
- Use imperative mood ("add", "fix", "remove" — not "added", "fixes")
- Body explains *why*, not *what* (the diff shows what)
- Never bundle unrelated changes in one commit

## Key Constraints

- **Soil ADC normalization:** Raw 0–4095 mapped to 0–100% using per-sensor dry/wet calibration stored in `Config`. Changing `ADC_DRY_DEFAULT`/`ADC_WET_DEFAULT` in constants.h affects first-boot defaults only.
- **Reed contact:** No software debounce — assumes stable hardware. The closing state relies entirely on this signal; flaky contact = roof timeout error.
- **PI controller anti-windup:** Integral is clamped in `lightManagement.cpp`. Changing lux targets mid-run can cause brief overshoot until integral settles. If BH1750 is absent, the controller runs feedforward-only (no integral accumulation).
- **Non-blocking loop:** All modules must return quickly. Never use `delay()` in module code — use timestamp-based state transitions (`millis()`).
- **Maintenance mode:** Enabled by default on boot. Regulation (roof/irrigation/light) is fully suppressed until the user disables it via the web UI. Manual control commands still work in maintenance mode.
- **Water level:** `ERR_WATER_CRITICAL` (lower switch) locks pump and all valves immediately. Low level (upper switch) logs a warning only and allows continued irrigation.
