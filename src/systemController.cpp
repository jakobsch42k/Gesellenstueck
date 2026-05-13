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

void SystemController::init() {
    Serial.begin(115200);
    delay(500); // wait for serial monitor to connect

    Serial.println("[systemController] booting...");

    // 1. Actuators first — all outputs off before anything else runs
    actuators_init();

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
    sensors_init(liveData, errorFlags, config);

    // 5. Control modules
    roofControl_init(liveData, errorFlags);
    irrigation_init();
    lightManagement_init();

    // 6. Web backend — starts WiFi AP and HTTP server
    webBackend_init(liveData, config, errorFlags);
    webBackend_registerCallbacks(
        [this](String cmd, int val) { handleManualCommand(cmd, val); },
        [this]()                    { handleAckErrors(); },
        [this](unsigned long ts)    { sensors_setTime(liveData, ts); }
    );

    errorFlags.MAINTENANCE_MODE = true;
    Serial.println("[systemController] boot complete — starting in maintenance mode");
}

void SystemController::run() {

    // 1. Read all sensors
    sensors_update(liveData, errorFlags, config);

    // 2. Critical fault handling — takes priority over everything
    if (errorFlags.EMERGENCY_STOP) {
        emergency_stop_all();
    } else if (errorFlags.ERR_WATER_CRITICAL) {
        pump_off();
        valve_closeAll();
    }

    // 3. Warn LED
    if (!liveData.waterCritical || errorFlags.ERR_WATER_CRITICAL) {
        led_warn_blink();
    } else if (!liveData.waterLow) {
        led_warn_on();
    } else {
        led_warn_off();
    }

    // 4. Regulation — skipped during critical faults or maintenance mode
    bool criticalFault = errorFlags.EMERGENCY_STOP ||
                         errorFlags.ERR_WATER_CRITICAL ||
                         errorFlags.ERR_FS_MOUNT      ||
                         errorFlags.MAINTENANCE_MODE;

    if (!criticalFault) {
        roofControl_update(liveData, config, errorFlags);
        irrigation_update(liveData, config, errorFlags);
        lightManagement_update(liveData, config, errorFlags);
    } else if (errorFlags.MAINTENANCE_MODE && liveData.roofClosed && !prevRoofClosed) {
        roof_stop();
        maintenanceOpenUntil = 0;
    } else if (maintenanceOpenUntil && millis() >= maintenanceOpenUntil) {
        roof_stop();
        maintenanceOpenUntil = 0;
    }
    prevRoofClosed = liveData.roofClosed;

    // 5. Display and web — always run so the UI stays responsive
    display_update(liveData, errorFlags);
    webBackend_handle();
}

// ── Manual command handler (called from webBackend /manual callback) ──────────

void SystemController::handleManualCommand(String cmd, int val) {
    Serial.println("[systemController] manual command: " + cmd +
                   (val ? " val=" + String(val) : ""));

    if      (cmd == "pump_on")        pump_on();
    else if (cmd == "pump_off")       pump_off();
    else if (cmd == "valve_open"  && val >= 1 && val <= 5) valve_open(val - 1);
    else if (cmd == "valve_close" && val >= 1 && val <= 5) valve_close(val - 1);
    else if (cmd == "valve_closeAll") valve_closeAll();
    else if (cmd == "roof_open") {
        roof_open();
        maintenanceOpenUntil = millis() + MAINTENANCE_OPEN_PULSE_MS;
    }
    else if (cmd == "roof_close") {
        if (!liveData.roofClosed) roof_close();
        else Serial.println("[systemController] roof_close ignored — reed already closed");
    }
    else if (cmd == "roof_stop")      roof_stop();
    else if (cmd == "led_pwm")        led_grow_setPWM(val);
    else if (cmd == "emergency_stop") {
        errorFlags.EMERGENCY_STOP    = true;
        errorFlags.lastErrorMessage  = "Emergency stop triggered via web UI";
        emergency_stop_all();
    }
    else if (cmd == "maintenance_on") {
        errorFlags.MAINTENANCE_MODE = true;
        roof_stop();
        Serial.println("[systemController] maintenance mode ON");
    }
    else if (cmd == "maintenance_off") {
        errorFlags.MAINTENANCE_MODE = false;
        lightManagement_resetPWM();
        Serial.println("[systemController] maintenance mode OFF");
    }
    else {
        Serial.println("[systemController] unknown manual command: " + cmd);
    }
}

// ── Error acknowledgement (called from webBackend /ackErrors callback) ────────

void SystemController::handleAckErrors() {
    errorFlags.ERR_ROOF_TIMEOUT   = false;
    errorFlags.ERR_WATER_CRITICAL = false;
    errorFlags.EMERGENCY_STOP     = false;
    errorFlags.lastErrorMessage   = "";

    roofControl_ackError(errorFlags);
    lightManagement_resetPWM();

    Serial.println("[systemController] errors acknowledged");
}
