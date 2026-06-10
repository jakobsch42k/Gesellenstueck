# S2: Stop actuators on FS fault

**Severity:** Critical | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`ERR_FS_MOUNT` is part of `criticalFault` (src/systemController.cpp:84–88), so regulation stops running — but nothing stops actuators that are *already* running. The active-stop block at src/systemController.cpp:67–71 only handles `EMERGENCY_STOP` and `ERR_WATER_CRITICAL`:

```cpp
if (errorFlags.EMERGENCY_STOP) {
    ...
} else if (errorFlags.ERR_WATER_CRITICAL) {
    ...
}
```

If the filesystem fails mid-run (flash corruption during a config save), a pump that was pumping, an open valve, or a moving roof motor continues indefinitely.

## Fix design

Extend the stop path so any critical fault actively parks the hardware. Either add `ERR_FS_MOUNT` (and `ERR_ROOF_TIMEOUT` — check whether the roof FSM already stops the motor itself when timing out; it does, but pump/valves are unrelated) to the chain, or restructure:

```cpp
bool criticalFault = errorFlags.EMERGENCY_STOP || errorFlags.ERR_WATER_CRITICAL ||
                     errorFlags.ERR_FS_MOUNT  || errorFlags.ERR_ROOF_TIMEOUT;
if (criticalFault) {
    pump_off();
    valve_closeAll();
    if (errorFlags.EMERGENCY_STOP || errorFlags.ERR_FS_MOUNT) roof_stop();
    // EMERGENCY_STOP additionally kills the grow light (existing behavior — keep it)
}
```

Keep the existing per-flag semantics: `ERR_WATER_CRITICAL` must not stop the roof (roof has nothing to do with water), `EMERGENCY_STOP` keeps its current full-stop behavior including `led_grow_setPWM(0)`. Calling `pump_off()`/`valve_closeAll()` every loop while a flag is set is idempotent and cheap — that is the existing pattern, keep it.

## Files

- `src/systemController.cpp` (run loop, lines ~67–88)

## Verification

1. `pio run`.
2. Desk test: with pump manually running (maintenance mode), simulate FS fault by setting `errorFlags.ERR_FS_MOUNT = true` via a temporary debug hook or by corrupting the mount in a test build — confirm pump and valves stop within one loop pass and stay stopped.
3. Confirm `/ackErrors` path still recovers as before.
