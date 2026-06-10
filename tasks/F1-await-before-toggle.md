# F1: Await POST before UI toggle

**Severity:** Critical (UI trust) | **Effort:** M | **Depends on:** — | **Status:** done

## Problem

Three controls in `data/script.js` flip local UI state optimistically, *before* the `/manual` POST is confirmed:

- **Maintenance toggle** (script.js:~638): `manual(...)` fires async, then `maintenance = !maintenance; updateMaintUI();` immediately. If the POST fails (only a toast in the catch), the UI shows "Maintenance ON" while the firmware is still regulating automatically — a lie about the system's safety state until the next poll corrects it.
- **Valve buttons** (script.js:~599): `valveState[i]` flipped optimistically.
- **Pump button** (script.js:~640): `pumpOn` flipped optimistically.

For controls that gate physical actuators, the UI must reflect confirmed state only.

## Fix design

Pattern for all three (check `manual()`'s current return — it should return the fetch promise; make it `return fetchT(...)` and throw on `!res.ok`):

```js
btn.addEventListener('click', async () => {
  btn.disabled = true;
  try {
    await manual(maintenance ? 'maintenance_off' : 'maintenance_on');
    maintenance = !maintenance;          // only after confirmed 200
    updateMaintUI();
  } catch (e) {
    toast('Command failed — state unchanged');
  } finally {
    btn.disabled = false;
  }
});
```

- Keep the existing poll reconciliation (`maintenance = !!d.MAINTENANCE_MODE` at script.js:~351) as the authoritative correction — it stays.
- Valve/pump: same await-then-mutate; disable the specific button during the round-trip (the 5 s `fetchT` AbortController timeout already bounds the wait).
- Emergency stop is exempt from disabling games — it should stay always-clickable; verify it already re-fires safely (it does — idempotent firmware-side).

## Files

- `data/script.js` (maintenance handler ~638, valve handler ~599, pump handler ~640, possibly `manual()` helper)

## Verification

1. `node --check data/script.js`.
2. Browser with device: toggle maintenance — button disables for the round-trip, state changes only on success.
3. Failure path: disconnect WiFi mid-click (or block the request in devtools) — UI must NOT flip; toast appears; next reconnect poll shows true state.
4. `pio run --target uploadfs` to deploy.
