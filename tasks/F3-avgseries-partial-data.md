# F3: avgSeries with partial data

**Severity:** High | **Effort:** S | **Depends on:** — | **Status:** done

## Problem

`avgSeries()` in `data/script.js` (~231):

```js
const n = Math.min(...hist.soil.map((s) => s.length));
```

If any one of the 5 soil channels never receives a sample (sensor dropout, `ERR_SOIL[bed]`), `n` stays 0 and the function returns `[]` forever. The dashboard soil trend shows "Collecting trend…" indefinitely even though 4 of 5 beds report fine. Functional blind spot, not a leak.

## Fix design

Average only channels that have data, aligned from the tail:

```js
function avgSeries() {
  const live = hist.soil.filter((s) => s.length > 0);
  if (!live.length) return [];
  const n = Math.min(...live.map((s) => s.length));
  const out = [];
  for (let i = 0; i < n; i++) {
    out.push(Math.round(live.reduce((a, s) => a + s[s.length - n + i], 0) / live.length));
  }
  return out;
}
```

Divisor changes from the literal `5` to `live.length` — also fixes the average being dragged down by silent-zero channels.

## Files

- `data/script.js` (avgSeries ~227–232)

## Verification

1. `node --check data/script.js`.
2. Quick unit check in node: stub `hist.soil = [[50,60],[40,50],[],[],[]]` → expect `[45,55]`; all-empty → `[]`.
3. Browser: with one soil sensor unplugged, dashboard soil trend must still render from remaining beds.
4. `pio run --target uploadfs`.
