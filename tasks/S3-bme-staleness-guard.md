# S3: BME280 staleness guard

**Severity:** High | **Effort:** M | **Depends on:** — | **Status:** done

## Problem

When the BME280 fails (NaN reads or I2C dropout), `sensors.cpp` keeps the last-good `tempC` and only sets `ERR_SENSOR_BME` — which is a warning flag, not part of `criticalFault` (src/systemController.cpp:84–88). The roof FSM keeps regulating on frozen data: a stale low reading (`tempC < tempTarget - hysteresis`, src/roofControl.cpp:52) closes the roof and keeps it closed while real temperature climbs. Worst case: sensor dies on a cool morning, sun comes out, roof never opens — plants cook.

## Fix design

1. **Track read age.** Add `unsigned long lastBmeReadMs` to `LiveData` (src/shared.h). In `sensors_update`, set it to `millis()` only on a *successful, non-NaN* BME read.
2. **Constant.** `#define SENSOR_STALE_TIMEOUT_MS 30000UL` in `src/constants.h` (30 s ≫ normal read cadence; tolerates transient I2C hiccups).
3. **Roof policy on stale data** (src/roofControl.cpp, inside `roofControl_update` before the switch):
   - If `millis() - data.lastBmeReadMs > SENSOR_STALE_TIMEOUT_MS`:
     - In ROOF_CLOSED / ROOF_IDLE → transition to opening (safe-open: an open roof can't overheat the bed; rain exposure is the lesser risk).
     - In ROOF_OPEN → hold (do not close on stale data).
     - Never start a CLOSING stroke on stale data.
   - Log once on stale-entry (latched bool), not every loop.

## Failure modes (control-logic checklist, per CLAUDE.md)

- **Stale data:** this task *is* the stale-data handler. Staleness threshold 30 s; policy = safe-open, never close.
- **Oscillation:** sensor flapping around the 30 s boundary could open the roof, then a recovered fresh-but-high reading keeps it open (consistent), or fresh-but-low reading closes it → at most one cycle per recovery. Acceptable; if combined with S6 min-dwell, dwell timer also damps this.
- **No-regulation:** while stale, temperature regulation is intentionally suspended in the safe (open) position; normal regulation resumes automatically on the first fresh read. `ERR_SENSOR_BME` continues to surface the fault in the UI.

Walk a concrete sequence before flashing: sensor dies at T+0 with tempC=18 °C frozen and roof CLOSED → at T+30 s roof opens (1.5 s pulse) → sensor recovers at T+5 min reading 31 °C → roof stays open (31 > target+hyst) → temp falls to 22 °C → normal close. Confirm no path closes the roof between T+30 s and recovery.

## Files

- `src/shared.h` (LiveData field)
- `src/sensors.cpp` (timestamp on good read)
- `src/constants.h` (timeout constant)
- `src/roofControl.cpp` (stale policy)

## Verification

1. `pio run`.
2. Bench test: boot with BME280 attached, confirm normal regulation. Disconnect SDA → within 30 s roof must open and hold; serial logs one staleness message. Reconnect → regulation resumes.
3. Confirm `ERR_SENSOR_BME` still shows in UI diagnostics throughout.
