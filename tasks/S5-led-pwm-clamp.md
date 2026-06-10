# S5: Clamp manual LED PWM

**Severity:** High | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

The `/manual` allowlist in `src/webBackend.cpp` accepts `led_pwm` but does not bound-check `value`. The raw client integer reaches `led_grow_setPWM(val)` (src/systemController.cpp:135). `led_grow_setPWM` may clamp internally (check src/actuators.cpp:~115) — but the contract should be enforced at the input boundary regardless: a crafted POST with `value: 99999` or negative must not depend on a downstream courtesy clamp.

## Fix design

In `handleManualCommand` (src/systemController.cpp:135):

```cpp
else if (cmd == "led_pwm")        led_grow_setPWM(constrain(val, 0, 255));
```

Additionally, in the webBackend `/manual` handler, reject out-of-range values for value-carrying commands with HTTP 400 (consistent with the existing allowlist 400 behavior): `led_pwm` → 0–255, `valve_open`/`valve_close` → 1–5 (verify valve range check exists; add if missing).

## Files

- `src/systemController.cpp` (clamp)
- `src/webBackend.cpp` (400 on out-of-range value)

## Verification

1. `pio run`.
2. `curl -X POST http://192.168.4.1/manual -d '{"command":"led_pwm","value":9999}'` → 400 (or clamped to 255 with 200 — pick one behavior and document it in the handler).
3. Normal slider use from the Control tab unaffected (0–255 passes through).
