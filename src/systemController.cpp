#include "systemController.h"
#include "actuators.h"
#include "display.h"
#include "fileManager.h"
#include "sensors.h"
#include "roofControl.h"
#include "irrigation.h"
#include "lightManagement.h"
#include "webBackend.h"
#include "constants.h"
#include <esp_task_wdt.h>

void SystemController::init() {
    Serial.begin(115200);
    delay(500); // wait for serial monitor to connect

    Serial.println("[systemController] booting...");

    // 1. Actuators first — all outputs off before anything else runs
    actuators.init();

    // 2. Display — gives visual feedback during boot
    display_init();

    // 3. Filesystem — config must be loaded before modules need it
    if (!fileManager_init()) {
        errorFlags.ERR_FS_MOUNT = true;
        Serial.println("[systemController] WARNING: filesystem unavailable");
    } else {
        loadConfig(config);
    }

    // 4. Sensors — populates liveData with initial values
    sensors.init(liveData, errorFlags, config);

    // 5. Control modules
    roofControl.init(liveData, errorFlags, actuators);
    irrigation.init(actuators);
    lightControl.init(actuators);

    // 6. Web backend — starts WiFi AP and HTTP server
    webBackend_init(liveData, config, errorFlags, controlStatus);
    webBackend_registerCallbacks(
        [this](String cmd, int val) { return handleManualCommand(cmd, val); },
        [this]()                    { handleAckErrors(); },
        [this](unsigned long ts)    { sensors.setTime(liveData, ts); }
    );

    // 7. Watchdog — last, so slow one-time init steps can't trip it. If the
    //    loop ever hangs (stuck I2C, held HTTP request), reset instead of
    //    leaving pump/valves/motor running unattended.
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    errorFlags.MAINTENANCE_MODE = true;
    Serial.println("[systemController] boot complete — starting in maintenance mode");
}

void SystemController::run() {

    esp_task_wdt_reset();

    // 1. Read all sensors
    sensors.update(liveData, errorFlags, config);

    // 1b. Latch critical water fault. waterCritical reads true when water is
    //     present; a false reading means the lower float tripped (tank empty).
    //     Latch so the lockout holds until the user acknowledges via /ackErrors.
    if (!liveData.waterCritical && !errorFlags.ERR_WATER_CRITICAL) {
        errorFlags.ERR_WATER_CRITICAL = true;
        errorFlags.lastErrorMessage   = "Water level critical — pump and valves locked";
        Serial.println("[systemController] ERR_WATER_CRITICAL latched — tank empty");
    }

    // 2. Critical fault handling — takes priority over everything.
    //    Any critical fault parks pump and valves; the roof motor is only
    //    forced off where it makes sense (water faults don't concern the
    //    roof, and the roof FSM already stops itself on its own timeout).
    if (errorFlags.EMERGENCY_STOP) {
        actuators.emergency_stop_all();
    } else if (errorFlags.ERR_WATER_CRITICAL ||
               errorFlags.ERR_FS_MOUNT      ||
               errorFlags.ERR_ROOF_TIMEOUT) {
        actuators.pump_off();
        actuators.valve_closeAll();
        if (errorFlags.ERR_FS_MOUNT) {
            actuators.roof_stop();
        }
    }

    // 3. Warn LED
    if (!liveData.waterCritical || errorFlags.ERR_WATER_CRITICAL) {
        actuators.led_warn_blink();
    } else if (!liveData.waterLow) {
        actuators.led_warn_on();
    } else {
        actuators.led_warn_off();
    }

    // 4. Regulation — skipped during critical faults or maintenance mode
    bool criticalFault = errorFlags.EMERGENCY_STOP ||
                         errorFlags.ERR_WATER_CRITICAL ||
                         errorFlags.ERR_FS_MOUNT      ||
                         errorFlags.ERR_ROOF_TIMEOUT  ||
                         errorFlags.MAINTENANCE_MODE;

    if (!criticalFault) {
        roofControl.update(liveData, config, errorFlags);
        irrigation.update(liveData, config, errorFlags);
        lightControl.update(liveData, config, errorFlags);
    } else if (errorFlags.MAINTENANCE_MODE && liveData.roofClosed && !prevRoofClosed) {
        actuators.roof_stop();
        roofControl.setState(ROOF_CLOSED);
        maintenanceOpenUntil = 0;
    } else if (maintenanceOpenUntil && millis() >= maintenanceOpenUntil) {
        actuators.roof_stop();
        roofControl.setState(ROOF_OPEN);
        maintenanceOpenUntil = 0;
    }
    prevRoofClosed = liveData.roofClosed;

    // 5. Snapshot control state for the web backend, then display and web —
    //    always run so the UI stays responsive
    controlStatus.roofState  = roofControl.getState();
    controlStatus.irrigState = irrigation.getState();
    controlStatus.lightPWM   = lightControl.getCurrentPWM();

    display_update(liveData, errorFlags);
    webBackend_handle();
}

// ── Manual command handler (called from webBackend /manual callback) ──────────

bool SystemController::handleManualCommand(String cmd, int val) {
    Serial.println("[systemController] manual command: " + cmd +
                   (val ? " val=" + String(val) : ""));

    if (cmd == "pump_on") {
        // Manual mode bypasses regulation, not physics — never run the pump
        // against an empty tank. Mirrors the auto-latch polarity in run():
        // waterCritical == true means water is sufficient.
        if (!liveData.waterCritical || errorFlags.ERR_WATER_CRITICAL) {
            Serial.println("[systemController] manual pump_on refused — water critical");
            return false;
        }
        actuators.pump_on();
    }
    else if (cmd == "pump_off")       actuators.pump_off();
    else if (cmd == "valve_open"  && val >= 1 && val <= NUM_BEDS) actuators.valve_open(val - 1);
    else if (cmd == "valve_close" && val >= 1 && val <= NUM_BEDS) actuators.valve_close(val - 1);
    else if (cmd == "valve_closeAll") actuators.valve_closeAll();
    else if (cmd == "roof_open") {
        actuators.roof_open();
        roofControl.setState(ROOF_OPENING);
        maintenanceOpenUntil = millis() + MAINTENANCE_OPEN_PULSE_MS;
    }
    else if (cmd == "roof_close") {
        if (!liveData.roofClosed) {
            actuators.roof_close();
            roofControl.setState(ROOF_CLOSING);
        } else Serial.println("[systemController] roof_close ignored — reed already closed");
    }
    else if (cmd == "roof_stop") {
        actuators.roof_stop();
        roofControl.setState(ROOF_IDLE);
    }
    else if (cmd == "led_pwm")        actuators.led_grow_setPWM(constrain(val, 0, 255));
    else if (cmd == "emergency_stop") {
        errorFlags.EMERGENCY_STOP    = true;
        errorFlags.lastErrorMessage  = "Emergency stop triggered via web UI";
        actuators.emergency_stop_all();
    }
    else if (cmd == "maintenance_on") {
        errorFlags.MAINTENANCE_MODE = true;
        actuators.roof_stop();
        Serial.println("[systemController] maintenance mode ON");
    }
    else if (cmd == "maintenance_off") {
        errorFlags.MAINTENANCE_MODE = false;
        lightControl.resetPWM();
        Serial.println("[systemController] maintenance mode OFF");
    }
    else {
        Serial.println("[systemController] unknown manual command: " + cmd);
    }
    return true;
}

// ── Error acknowledgement (called from webBackend /ackErrors callback) ────────

void SystemController::handleAckErrors() {
    errorFlags.ERR_ROOF_TIMEOUT   = false;
    errorFlags.ERR_WATER_CRITICAL = false;
    errorFlags.EMERGENCY_STOP     = false;
    errorFlags.lastErrorMessage   = "";

    roofControl.ackError(errorFlags);
    lightControl.resetPWM();

    Serial.println("[systemController] errors acknowledged");
}
