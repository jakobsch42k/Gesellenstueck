# F6: localStorage schema versioning

**Severity:** Medium | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`data/script.js` stores bed-plant assignments under `PLANT_KEY = 'hb_bedplants'` (~147) as a raw `string[5]` array. No schema version. The `try/catch` added around `JSON.parse` (~151) guards malformed JSON only — a *structurally* different value (key reuse, bed-count change, shape change) parses fine and flows into rendering with wrong shape. Any future change (6th bed, per-bed objects instead of strings) silently corrupts returning users' state.

## Fix design

Wrap with a version envelope and a single load/save helper:

```js
const PLANT_SCHEMA_V = 1;

function loadBedPlants() {
  try {
    const raw = JSON.parse(localStorage.getItem(PLANT_KEY));
    if (raw && raw.v === PLANT_SCHEMA_V && Array.isArray(raw.data) && raw.data.length === 5)
      return raw.data;
    // legacy migration: pre-envelope raw array
    if (Array.isArray(raw) && raw.length === 5) return raw;
  } catch (e) {}
  return [null, null, null, null, null];
}

function saveBedPlants(arr) {
  localStorage.setItem(PLANT_KEY, JSON.stringify({ v: PLANT_SCHEMA_V, data: arr }));
}
```

- Migration path: existing users' raw arrays are accepted once and re-saved in envelope form on next change.
- Route all existing read/write sites of `PLANT_KEY` through these helpers (grep `PLANT_KEY` for all sites).
- `THEME_KEY` stores a plain string with a closed value set — leave it; validate against the allowed set at the read site if not already done.

## Files

- `data/script.js` (PLANT_KEY sites ~147–151 + write sites)

## Verification

1. `node --check data/script.js`.
2. Browser: with legacy raw-array value in localStorage, page loads assignments correctly (migration); change a bed assignment, inspect localStorage — envelope form present; reload — persists.
3. Corrupt the value by hand (`localStorage.setItem('hb_bedplants','{"v":99}')`) — page falls back to empty assignments without console errors.
4. `pio run --target uploadfs`.
