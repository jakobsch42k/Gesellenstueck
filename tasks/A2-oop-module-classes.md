# A2: OOP module classes

**Severity:** Medium (architecture) | **Effort:** L | **Depends on:** A1, S1–S8 (fixes carry into refactored code) | **Status:** done

## Problem

Every hardware/control module is C-style: file-scoped `static` state + free functions — `roofControl.cpp:4–5`, `irrigation.cpp:4–6`, `lightManagement.cpp:13–17`, `sensors.cpp:8–22`, plus `actuators.cpp`. Consequences:

- No encapsulation; state dependencies implicit (user preference is OOP — see global CLAUDE.md).
- Unit testing impossible (cannot instantiate two, cannot inject fakes).
- `webBackend.cpp` reaches into control internals directly: `roofControl_getState()` (192), `irrigation_getState()` (399), `lightManagement_getCurrentPWM()` (421) — compile-time coupling of the HTTP layer to three control modules.
- `webBackend.cpp:17–19` keeps raw pointers to `LiveData`/`Config`/`ErrorFlags`.

`SystemController` (src/systemController.cpp) is already a class — the pattern to follow exists in-repo.

## Fix design

**Splittable: one module per session/commit. Order: Actuators → Sensors → RoofController → IrrigationController → LightController → webBackend decoupling.** Each step compiles and flashes independently.

Per-module conversion recipe (same every time):

1. Create class in the module's header, e.g.:
   ```cpp
   class RoofController {
   public:
       void init(LiveData* data, Config* cfg, ErrorFlags* err);
       void update();
       RoofState getState() const { return state; }
       void setState(RoofState s);
       void ackError();
   private:
       void enterState(RoofState s);
       RoofState state = ROOF_IDLE;
       unsigned long stateEnteredAt = 0;
       LiveData* data = nullptr; Config* cfg = nullptr; ErrorFlags* err = nullptr;
   };
   ```
2. Move each file-static into a private member; each `roofControl_*` free function becomes a method (body unchanged — this is a mechanical move, not a rewrite).
3. `SystemController` owns the instances as members and calls methods instead of free functions.
4. Keep the existing non-blocking/millis() patterns and all S-task fixes verbatim.

**webBackend decoupling (last step):** introduce a read-only snapshot struct in `shared.h`:

```cpp
struct ControlStatus { RoofState roofState; IrrigationState irrigState; int lightPWM; };
```

`SystemController` fills it each loop; webBackend receives `const ControlStatus*` at init like it receives `LiveData*` today, and drops its includes of roofControl/irrigation/lightManagement headers. (Raw-pointer wiring itself stays — objects live for program lifetime inside SystemController; document that lifetime contract in webBackend.h.)

**Actuators note:** actuator functions are stateless wrappers over GPIO except blink state — an `Actuators` class is justified mainly for symmetry and testability; converting it first is the lowest-risk warm-up.

## Files

- All of: `src/roofControl.{h,cpp}`, `src/irrigation.{h,cpp}`, `src/lightManagement.{h,cpp}`, `src/sensors.{h,cpp}`, `src/actuators.{h,cpp}`, `src/systemController.{h,cpp}`, `src/webBackend.{h,cpp}`, `src/shared.h`

## Verification (per module, every session)

1. `pio run` after each module conversion — must compile clean.
2. Flash + functional spot check for the converted module (roof: manual open/close; irrigation: dry-bed cycle in test; light: PWM tracks profile).
3. Behavior must be identical — this is a structure-only refactor; any logic change belongs in its own commit.
4. After webBackend decoupling: `grep -n "roofControl_\|irrigation_\|lightManagement_" src/webBackend.cpp` → no hits.
