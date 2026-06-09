# S6: Roof min-dwell + reed abort during opening

**Severity:** High | **Effort:** M | **Depends on:** — | **Status:** open

## Problem

Two related FSM gaps in `src/roofControl.cpp`:

1. **No minimum dwell.** ROOF_CLOSED (line 32–38) and ROOF_OPEN (line 50–57) re-evaluate temperature on the very next loop pass after a stroke completes. A reading hovering at the threshold (±sensor noise around `tempTarget ± tempHysteresis`) produces open/close oscillation at full motor PWM. The 1 °C default hysteresis gives only a 2 °C band — noise plus thermal lag can cross it repeatedly.
2. **No reed abort while opening.** ROOF_OPENING (line 41–47) never checks `data.roofClosed`. The stroke is purely time-based (`roofOpenDuration_ms`); a mechanical fault or spurious reed signal during opening is invisible.

## Fix design

1. **Dwell:** `#define ROOF_MIN_DWELL_MS 60000UL` in `src/constants.h`. In ROOF_CLOSED and ROOF_OPEN cases, gate the temperature comparison on `elapsed >= ROOF_MIN_DWELL_MS` (the FSM already computes `elapsed = millis() - stateEnteredAt` at line 27, and `enterState` stamps `stateEnteredAt` — reuse, no new state).
   - Exception: do NOT delay the *first* regulation decision after boot longer than necessary — acceptable; 60 s after boot is fine.
   - Manual commands and the S3 stale-data safe-open must bypass the dwell (safety beats anti-chatter). Order the checks: stale-policy first, then dwell gate, then temperature comparison.
2. **Reed abort:** in ROOF_OPENING, if `data.roofClosed` is still/again true after a grace period (e.g. `elapsed > 500 ms` — the contact needs time to physically release), log a warning. Decide policy: continue the stroke (reed may be sticky) but set a diagnostic flag, or abort to ROOF_ERROR. Recommended: log + continue on first occurrence; this avoids bricking the roof on a flaky contact while making the fault visible. Document the choice in the code comment.

## Failure modes (control-logic checklist, per CLAUDE.md)

- **Stale data:** unchanged by this task — handled by S3; dwell gate must sit *after* the stale-policy check so safe-open is never delayed.
- **Oscillation:** this task is the oscillation fix. Worst-case cycle rate drops from per-loop (~10 ms) to one transition per 60 s.
- **No-regulation:** dwell delays a legitimate transition by ≤ 60 s. Thermal time constant of a raised bed is minutes — 60 s delay is negligible for temperature control, and the open pulse is only 1.5 s of travel.

Paper walk-through: temp ramps past `target+hyst` at T+0 → roof opens (was closed > 60 s) → temp keeps rising → stays open (no transition available) → cloud, temp falls through `target-hyst` at T+3 min → dwell satisfied (open since T+0+1.5 s) → closes. Noise case: temp jitters ±0.3 °C around `target+hyst` → at most one open per 60 s instead of chatter.

## Files

- `src/roofControl.cpp`
- `src/constants.h` (ROOF_MIN_DWELL_MS)

## Verification

1. `pio run`.
2. Bench with heat source: drive temp across the open threshold repeatedly within a minute — confirm exactly one motor stroke per dwell window (serial logs state transitions).
3. Confirm manual roof commands in maintenance mode still respond immediately.
4. Confirm `ERR_ROOF_TIMEOUT` path and `/ackErrors` recovery unchanged.
