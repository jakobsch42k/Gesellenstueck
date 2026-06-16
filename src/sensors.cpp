#include "sensors.h"
#include "constants.h"
#include "logger.h"
#include "pins.h"
#include <Wire.h>

// Soil sensor ADC pins in index order
static const int SOIL_PINS[NUM_BEDS] = {
    SOIL_PIN_1, SOIL_PIN_2, SOIL_PIN_3, SOIL_PIN_4, SOIL_PIN_5
};

// ── Init ──────────────────────────────────────────────────────────────────────

void Sensors::init(LiveData& data, ErrorFlags& err, const Config& cfg) {
    Wire.begin(PIN_SDA, PIN_SCL);

    // BME280
    if (!bme.begin(BME_I2C_ADDR_PRIMARY)) {
        // Try alternate address
        if (!bme.begin(BME_I2C_ADDR_ALT)) {
            err.ERR_SENSOR_BME = true;
            err.lastErrorMessage = "BME280 not found on I2C";
            logger.error("sensors", "BME280 not found on I2C");
        }
    }
    if (!err.ERR_SENSOR_BME) {
        logger.info("sensors", "BME280 OK");
    }

    // BH1750
    if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
        err.ERR_SENSOR_BH = true;
        err.lastErrorMessage = "BH1750 not found on I2C";
        logger.error("sensors", "BH1750 not found on I2C");
    } else {
        logger.info("sensors", "BH1750 OK");
    }

    // Digital inputs — external pull-down resistors on PCB, no internal pull needed
    pinMode(WATER_LOW_PIN,      INPUT);
    pinMode(WATER_CRITICAL_PIN, INPUT);
    pinMode(REED_PIN, INPUT);

    // Safe defaults so no error flags or shutdowns trigger
    data.waterLow      = true;
    data.waterCritical = true;
    data.roofClosed    = false;

    for (int i = 0; i < NUM_BEDS; i++) {
        data.soilRaw[i]  = 0;
        data.soilPerc[i] = 0;
    }

    // Do an initial read to populate LiveData before first loop
    update(data, err, cfg);

    lastTimeUpdate = millis();
    Serial.println("[sensors] init OK");
}

// ── Update ────────────────────────────────────────────────────────────────────

void Sensors::update(LiveData& data, ErrorFlags& err, const Config& cfg) {

    // ── Advance timeOfDay ────────────────────────────────────────────────────
    unsigned long now = millis();
    unsigned long elapsed = (now - lastTimeUpdate) / 1000; // whole seconds only
    if (elapsed > 0) {
        data.timeOfDay = (data.timeOfDay + elapsed) % 86400;
        lastTimeUpdate += elapsed * 1000;
    }

    // ── BME280 ───────────────────────────────────────────────────────────────
    if (!err.ERR_SENSOR_BME) {
        float t = bme.readTemperature();
        float h = bme.readHumidity();
        // NaN indicates a read failure — keep last good value
        if (!isnan(t) && !isnan(h)) {
            data.tempC         = t;
            data.humPerc       = h;
            data.lastBmeReadMs = millis();
        } else {
            Serial.println("[sensors] WARNING: BME280 read returned NaN");
        }
    }

    // ── BH1750 ───────────────────────────────────────────────────────────────
    // Gated to sensor integration time — reading faster returns the same value
    if (!err.ERR_SENSOR_BH && (millis() - lastLuxSample >= LUX_SAMPLE_INTERVAL_MS)) {
        float lux = lightMeter.readLightLevel();
        if (lux >= 0) {
            data.lux = lux;
            if (!luxEmaSeeded) {
                data.luxSmoothed = lux;     // seed so we don't ramp from 0
                luxEmaSeeded = true;
            } else {
                data.luxSmoothed = LUX_EMA_ALPHA * lux + (1.0f - LUX_EMA_ALPHA) * data.luxSmoothed;
            }
            lastLuxSample = millis();
        } else {
            Serial.println("[sensors] WARNING: BH1750 read failed");
        }
    }

    // ── Soil moisture sensors ────────────────────────────────────────────────
    for (int i = 0; i < NUM_BEDS; i++) {
        long sum = 0;
        for (int s = 0; s < SOIL_ADC_SAMPLES; s++) {
            sum += analogRead(SOIL_PINS[i]);
        }
        int raw = (int)(sum / SOIL_ADC_SAMPLES);
        data.soilRaw[i] = raw;

        if (raw < SOIL_ADC_MIN || raw > SOIL_ADC_MAX) {
            // Edge-log: only on the false→true transition so a persistently
            // faulty sensor doesn't write a journal line every loop pass.
            if (!err.ERR_SOIL[i]) {
                logger.warn("sensors", "soil sensor " + String(i + 1) +
                            " reading implausible (raw " + String(raw) + ")");
            }
            err.ERR_SOIL[i] = true;
        } else {
            if (err.ERR_SOIL[i]) {
                logger.info("sensors", "soil sensor " + String(i + 1) + " reading recovered");
            }
            err.ERR_SOIL[i] = false;
            int perc = map(raw, cfg.soilDryValue, cfg.soilWetValue, 0, 100);
            data.soilPerc[i] = constrain(perc, 0, 100);
        }
    }

    // ── Digital inputs ───────────────────────────────────────────────────────
    data.waterLow      = !digitalRead(WATER_LOW_PIN);      // ext pull-down; HIGH = closed = empty, LOW = open = sufficient
    data.waterCritical = !digitalRead(WATER_CRITICAL_PIN); // ext pull-down; HIGH = closed = empty, LOW = open = sufficient
    data.roofClosed = digitalRead(REED_PIN);                 // HIGH = closed
}

// ── Time setter (called by webBackend /setTime) ───────────────────────────────

void Sensors::setTime(LiveData& data, unsigned long unixTimestamp) {
    // Extract seconds since midnight from Unix timestamp (UTC)
    data.timeOfDay = unixTimestamp % 86400;
    lastTimeUpdate = millis();
    Serial.println("[sensors] timeOfDay set to " + String(data.timeOfDay) + "s");
}
