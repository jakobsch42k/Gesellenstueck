# F2: Falsy-zero config guards

**Severity:** High | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`loadConfig` in `data/script.js` (~390–396) uses `||` fallbacks:

```js
config.luxTarget = c.luxTarget || c.lux || 800;
config.moisture  = c.moisture || [...].map(x => x || 50);
```

`0` is a valid value (`luxTarget: 0` = lights fully off; a moisture target of 0 is invalid but per-element `x || 50` also clobbers legit falsy handling inconsistently). With `||`, a stored 0 silently becomes 800 — then a subsequent "Save configuration" writes 800 back to the firmware, destroying the user's setting through the UI round-trip.

## Fix design

Replace falsy checks with nullish checks throughout `loadConfig`:

```js
config.luxTarget  = c.luxTarget ?? c.lux ?? 800;
config.tempTarget = c.tempTarget ?? 24;
config.tempHyst   = c.tempHysteresis ?? c.tempHyst ?? 1;
config.moisture   = Array.isArray(c.moisture) && c.moisture.length === 5
                    ? c.moisture.map(x => (x ?? 50))
                    : [c.moisture1, c.moisture2, c.moisture3, c.moisture4, c.moisture5].map(x => x ?? 50);
```

Audit every `||`-with-numeric-fallback in script.js while in there (`grep -n "|| [0-9]" data/script.js`) and convert any where 0 is a legitimate value. Leave string/array fallbacks where falsy semantics are intended.

`??` is supported by every browser that runs the rest of this script (optional chaining is likely already used — confirm; if targeting very old WebViews, use explicit `!= null` ternaries instead).

## Files

- `data/script.js` (loadConfig ~390–396 + grep sweep)

## Verification

1. `node --check data/script.js`.
2. Set `luxTarget` to 0 via the UI stepper (or curl `/saveConfig`), reload the page — UI must show 0, not 800; saving again must keep 0 (confirm via `GET /config`).
3. `pio run --target uploadfs`.
