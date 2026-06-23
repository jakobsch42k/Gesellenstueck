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

    // BME280 (tries primary then alternate I2C address)
    if (!beginBme()) {
        err.ERR_SENSOR_BME = true;
        err.lastErrorMessage = "BME280 not found on I2C";
        logger.error("sensors", "BME280 not found on I2C");
    } else {
        logger.info("sensors", "BME280 OK");
    }

    // BH1750
    if (!beginBh()) {
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

    // Seed debouncers to the same safe defaults so the first committed value
    // can't be a transient noise spike.
    waterLowInput.seed(true);
    waterCriticalInput.seed(true);
    reedInput.seed(false);

    for (int i = 0; i < NUM_BEDS; i++) {
        data.soilRaw[i]  = 0;
        data.soilPerc[i] = 0;
    }

    // Do an initial read to populate LiveData before first loop
    update(data, err, cfg);

    lastTimeUpdate = millis();
    Serial.println("[sensors] init OK");
}

// ── I2C (re)init helpers ───────────────────────────────────────────────────────
// Shared by init() and the runtime fault-recovery path so the begin sequence
// stays in one place.
bool Sensors::beginBme() {
    if (bme.begin(BME_I2C_ADDR_PRIMARY)) { bmeAddr = BME_I2C_ADDR_PRIMARY; return true; }
    if (bme.begin(BME_I2C_ADDR_ALT))     { bmeAddr = BME_I2C_ADDR_ALT;     return true; }
    return false;
}

bool Sensors::beginBh() {
    return lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
}

bool Sensors::i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;   // 0 = device ACKed
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
    // Detect presence by I2C ACK every loop: a disconnected BME280 returns finite
    // garbage (not NaN), so a read-value check alone can't see a runtime unplug.
    // Debounced both ways (hysteresis) so a single bus glitch can't flap the flag.
    {
        bool present = (bmeAddr && i2cPresent(bmeAddr)) ||
                       i2cPresent(BME_I2C_ADDR_PRIMARY) || i2cPresent(BME_I2C_ADDR_ALT);
        if (present) {
            bmeFail = 0;
            if (err.ERR_SENSOR_BME) {
                // Recovering: re-init (sensor may have power-cycled) then clear
                // only after a few clean cycles so we don't trust garbage early.
                if (++bmeOk >= SENSOR_OK_THRESHOLD) {
                    beginBme();
                    err.ERR_SENSOR_BME = false;
                    bmeOk = 0;
                    logger.info("sensors", "BME280 recovered");
                }
            } else {
                float t = bme.readTemperature();
                float h = bme.readHumidity();
                if (!isnan(t) && !isnan(h)) {   // keep last-good on a transient NaN
                    data.tempC         = t;
                    data.humPerc       = h;
                    data.lastBmeReadMs = millis();
                }
            }
        } else {
            bmeOk = 0;                          // hold last-good reading
            if (!err.ERR_SENSOR_BME && ++bmeFail >= SENSOR_FAIL_THRESHOLD) {
                err.ERR_SENSOR_BME = true;
                err.lastErrorMessage = "BME280 stopped responding";
                logger.error("sensors", "BME280 stopped responding");
            }
        }
    }

    // ── BH1750 ───────────────────────────────────────────────────────────────
    // Same presence-based detect/recover as BME, gated to integration time.
    if (millis() - lastLuxSample >= LUX_SAMPLE_INTERVAL_MS) {
        lastLuxSample = millis();
        if (i2cPresent(BH_I2C_ADDR)) {
            bhFail = 0;
            if (err.ERR_SENSOR_BH) {
                if (++bhOk >= SENSOR_OK_THRESHOLD) {
                    beginBh();
                    err.ERR_SENSOR_BH = false;
                    bhOk = 0;
                    luxEmaSeeded = false;       // re-seed EMA from the next read
                    logger.info("sensors", "BH1750 recovered");
                }
            } else {
                float lux = lightMeter.readLightLevel();
                if (lux >= 0) {
                    data.lux = lux;
                    if (!luxEmaSeeded) {
                        data.luxSmoothed = lux; // seed so we don't ramp from 0
                        luxEmaSeeded = true;
                    } else {
                        data.luxSmoothed = LUX_EMA_ALPHA * lux + (1.0f - LUX_EMA_ALPHA) * data.luxSmoothed;
                    }
                }
            }
        } else {
            bhOk = 0;
            if (!err.ERR_SENSOR_BH && ++bhFail >= SENSOR_FAIL_THRESHOLD) {
                err.ERR_SENSOR_BH = true;
                err.lastErrorMessage = "BH1750 stopped responding";
                logger.error("sensors", "BH1750 stopped responding");
            }
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
    // Raw reads (ext pull-down; HIGH = closed). No hardware filter, so each is
    // debounced before committing to LiveData — see DIGITAL_DEBOUNCE_MS.
    now = millis();
    bool waterLowRaw      = !digitalRead(WATER_LOW_PIN);      // HIGH = closed = empty, LOW = open = sufficient
    bool waterCriticalRaw = !digitalRead(WATER_CRITICAL_PIN); // HIGH = closed = empty, LOW = open = sufficient
    bool reedRaw          = digitalRead(REED_PIN);            // HIGH = closed

    data.waterLow      = waterLowInput.update(waterLowRaw, now, DIGITAL_DEBOUNCE_MS);
    data.waterCritical = waterCriticalInput.update(waterCriticalRaw, now, DIGITAL_DEBOUNCE_MS);
    data.roofClosed    = reedInput.update(reedRaw, now, DIGITAL_DEBOUNCE_MS);
}

// ── Time setter (called by webBackend /setTime) ───────────────────────────────

void Sensors::setTime(LiveData& data, unsigned long unixTimestamp) {
    // Extract seconds since midnight from Unix timestamp (UTC)
    data.timeOfDay = unixTimestamp % 86400;
    lastTimeUpdate = millis();
    Serial.println("[sensors] timeOfDay set to " + String(data.timeOfDay) + "s");
}
