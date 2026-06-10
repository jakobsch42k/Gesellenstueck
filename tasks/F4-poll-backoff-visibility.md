# F4: Poll backoff + visibility pause + staleness indicator

**Severity:** Medium | **Effort:** M | **Depends on:** — | **Status:** done

## Problem

`data/script.js` (~659): `setInterval(poll, 2000)` runs unconditionally forever.

1. **No visibility pause** — a backgrounded phone tab polls 3 endpoints every 2 s, draining battery and loading the single-threaded ESP32 `WebServer` for nothing.
2. **No backoff** — when the device is unreachable, the same 3 requests fire every 2 s (each waiting up to the 5 s AbortController timeout).
3. **Stale `lastDiag` invisible** (script.js:~364–365) — `/diagnostics` failure is swallowed (`catch (e) {}`), the Sensors tab keeps rendering the last successful payload with no staleness cue. (`/systemStatus` failure at least renders null.)

The existing OFFLINE badge (driven by `/data.json` failure) covers the main poll; `/diagnostics` can fail independently.

## Fix design

Replace fixed `setInterval` with a self-scheduling loop:

```js
let pollDelay = 2000;
async function pollLoop() {
  if (document.hidden) { setTimeout(pollLoop, 1000); return; } // cheap idle check
  await poll();
  pollDelay = lastPollOk ? 2000 : Math.min(pollDelay * 2, 30000); // reset on success
  setTimeout(pollLoop, pollDelay);
}
pollLoop();
document.addEventListener('visibilitychange', () => { if (!document.hidden) { pollDelay = 2000; } });
```

- `poll()` already sets success/failure state for the OFFLINE badge — expose that as `lastPollOk` instead of duplicating.
- On visibility regain, poll immediately (reset delay; optionally call `poll()` directly).
- Staleness: record `lastDiagAt = Date.now()` on each successful `/diagnostics`; in `renderDiag`, if `Date.now() - lastDiagAt > 10000`, add a muted "data N s old" note (reuse `.note` styling) instead of pretending freshness.

Keep the re-entrancy guard (`polling` flag) — it still protects against overlap.

## Files

- `data/script.js` (poll scheduling ~659, pollOnce ~341–371, renderDiag)

## Verification

1. `node --check data/script.js`.
2. Browser devtools network tab: background the tab → requests stop within a second; foreground → immediate poll, 2 s cadence resumes.
3. Power off the device → request cadence visibly stretches (2→4→8→…→30 s); power on → recovers to 2 s after first success.
4. Block only `/diagnostics` (devtools request blocking) → Sensors tab shows staleness note within ~10 s.
5. `pio run --target uploadfs`.
