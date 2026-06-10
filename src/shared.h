#pragma once

#include <Arduino.h>
#include "constants.h"

// ── LiveData ──────────────────────────────────────────────────────────────────
// Written exclusively by sensors.cpp; all other modules read only.
struct LiveData {
    float         tempC;          // Temperature in °C
    float         humPerc;        // Relative humidity in %
    float         lux;            // Current light level in lux
    float         luxSmoothed;    // 60-second rolling average of lux
    int           soilRaw[NUM_BEDS];   // ADC raw value 0–4095 per bed
    int           soilPerc[NUM_BEDS];  // Normalised soil moisture 0–100% per bed
    bool          waterLow;       // true = water sufficient (BG1 HIGH)
    bool          waterCritical;  // true = water sufficient (BG2 HIGH)
    bool          roofClosed;     // true = roof in end position (Reed HIGH)
    unsigned long timeOfDay;      // Seconds since midnight (0–86399)
    unsigned long lastBmeReadMs;  // millis() of last successful non-NaN BME280 read
};

// ── Config ────────────────────────────────────────────────────────────────────
// Loaded from config.json by fileManager; persisted on LittleFS.
//
// CANONICAL FIELD SET. When adding/removing a field here, update ALL of:
//   1. fileManager.cpp  configToJson()      — persistence writer
//   2. fileManager.cpp  jsonToConfig()       — persistence reader
//   3. webBackend.cpp   applyJsonToConfig()  — web apply
//   4. webBackend.cpp   validateConfig()     — web input bounds
// These four enumerate the fields by hand and silently drift if one is missed.
struct Config {
    // Bed setpoints
    int           moisture[NUM_BEDS];    // Target soil moisture 0–100% per bed

    // Roof temperature control
    float         tempTarget;             // Target temperature in °C
    float         tempHysteresis;         // Hysteresis band in °C

    // Light management
    int           luxTarget;              // Base lux setpoint
    int           lightProfile[24];       // Desired brightness % per hour (0 = LED off)

    // Timing — kept as variables so they can be tuned via config
    unsigned long roofOpenDuration_ms;    // Motor run time for opening
    unsigned long roofCloseTimeout_ms;    // Max wait for reed contact when closing
    unsigned long irrigationDuration_ms;  // Pump run time per irrigation cycle
    unsigned long irrigationPause_ms;     // Diffusion pause between cycles

    // ADC calibration for capacitive soil sensors
    int           soilDryValue;           // ADC count for completely dry sensor
    int           soilWetValue;           // ADC count for saturated sensor

    // Schema version — used by fileManager for future migrations
    int           configVersion;
};

// ── Control module states ─────────────────────────────────────────────────────
// Defined here (not in the controller headers) so ControlStatus below can be
// shared with webBackend without coupling it to the control modules.
enum RoofState {
    ROOF_IDLE,
    ROOF_OPENING,
    ROOF_OPEN,
    ROOF_CLOSING,
    ROOF_CLOSED,
    ROOF_ERROR
};

enum IrrigationState {
    IRRIGATION_IDLE,
    IRRIGATION_PUMPING,
    IRRIGATION_PAUSING
};

// ── ControlStatus ─────────────────────────────────────────────────────────────
// Read-only snapshot of control module state for the web backend. Filled by
// SystemController each loop; webBackend reads it via const pointer.
struct ControlStatus {
    RoofState       roofState  = ROOF_IDLE;
    IrrigationState irrigState = IRRIGATION_IDLE;
    int             lightPWM   = 0;
};

// ── ErrorFlags ────────────────────────────────────────────────────────────────
// Critical flags lock actuators until manually acknowledged via /ackErrors.
// Warning flags clear automatically when the cause is resolved.
struct ErrorFlags {
    // Critical — automatic operation locked
    bool   ERR_ROOF_TIMEOUT;    // Roof did not reach end position within timeout
    bool   ERR_WATER_CRITICAL;  // BG2 triggered → pump and valves locked
    bool   ERR_FS_MOUNT;        // LittleFS could not be mounted
    bool   EMERGENCY_STOP;      // Manual emergency stop via web UI
    bool   MAINTENANCE_MODE;    // Automatic regulation paused; manual control only

    // Operational — reduced function
    bool   ERR_SENSOR_BME;      // BME280 unreachable on I2C
    bool   ERR_SENSOR_BH;       // BH1750 unreachable on I2C
    bool   ERR_SOIL[NUM_BEDS];  // ADC value outside plausibility range per bed

    // Human-readable description of the most recent error (shown in web UI)
    String lastErrorMessage;
};
