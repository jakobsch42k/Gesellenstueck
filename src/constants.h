#pragma once

#include "pins.h"
#include <stdint.h>

// ── System Dimensions ────────────────────────────────────────────────────────
constexpr uint8_t NUM_BEDS = 5;   // Raised beds: soil sensors, valves, moisture targets

// ── WiFi Credentials ─────────────────────────────────────────────────────────
#define WIFI_SSID                  "Esp-32-Webserver"
#define WIFI_PASS                  "12345678"

// ── Config Defaults ──────────────────────────────────────────────────────────
#define DEFAULT_MOISTURE            40    // Target soil moisture per bed (%)
#define DEFAULT_TEMP_TARGET         25.0f // Target temperature (°C)
#define DEFAULT_TEMP_HYSTERESIS      1.0f // Hysteresis band (°C)
#define DEFAULT_LUX_TARGET         180    // Lux measured at 100% LED power (calibration)

// ── Timing Defaults (all in milliseconds) ────────────────────────────────────
#define DEFAULT_ROOF_OPEN_MS       1500UL  // Motor run time to open roof (auto mode)
#define MAINTENANCE_OPEN_PULSE_MS  1500UL  // Pulse duration for manual open button
#define DEFAULT_ROOF_CLOSE_TIMEOUT 5000UL  // Max wait for reed contact when closing
#define ROOF_MIN_DWELL_MS         60000UL  // Min time in OPEN/CLOSED before next auto stroke (anti-oscillation)
#define ROOF_REED_GRACE_MS          500UL  // Reed must physically release within this after open starts
#define DEFAULT_IRRIGATE_MS       10000UL  // Pump run time per irrigation cycle
#define DEFAULT_IRRIGATE_PAUSE   600000UL  // Diffusion pause between cycles

// ── ADC Calibration ──────────────────────────────────────────────────────────
#define DEFAULT_SOIL_DRY           3500   // ADC count for completely dry sensor
#define DEFAULT_SOIL_WET            800   // ADC count for saturated sensor
#define SOIL_ADC_MIN                 50   // Below this → sensor fault
#define SOIL_ADC_MAX               4000   // Above this → sensor fault
#define SOIL_ADC_SAMPLES             10   // Averaged readings per sensor per cycle

// ── Sensor staleness ─────────────────────────────────────────────────────────
// Max age of last good BME280 read before temperature data counts as stale.
// 30 s ≫ normal read cadence (every loop pass); tolerates transient I2C hiccups.
#define SENSOR_STALE_TIMEOUT_MS    30000UL

// ── Light Management ─────────────────────────────────────────────────────────
#define LUX_SAMPLE_INTERVAL_MS      120   // BH1750 high-res integration time
#define LUX_EMA_ALPHA               0.15f // EMA weight per sample (≈6-sample window ≈ 700 ms)
#define LUX_HYSTERESIS                5   // Dead-band for P-controller (lux)

// ── Watchdog ─────────────────────────────────────────────────────────────────
// Generous: normal loop pass is ms; worst case (importConfig + backup rotation
// on degraded flash) stays well under this. Do not go below ~3 s.
#define WDT_TIMEOUT_S                 8

// ── Display ──────────────────────────────────────────────────────────────────
#define DISPLAY_UPDATE_MS          2000UL // LCD refresh interval

// ── Config File ──────────────────────────────────────────────────────────────
#define CONFIG_VERSION                1
#define CONFIG_PATH            "/config.json"
#define CONFIG_TMP_PATH        "/config.tmp"
#define CONFIG_BAK1_PATH       "/config.bak1"
#define CONFIG_BAK2_PATH       "/config.bak2"
#define CONFIG_BAK3_PATH       "/config.bak3"
#define PLANTS_PATH            "/plants.json"

// ── Web Input Bounds (config validation) ─────────────────────────────────────
// Guard rails for values written via /saveConfig and /importConfig so a bad
// client or corrupt import cannot, e.g., make the pump run effectively forever.
#define ROOF_OPEN_MS_MIN           200UL
#define ROOF_OPEN_MS_MAX         30000UL
#define ROOF_CLOSE_MS_MIN          500UL
#define ROOF_CLOSE_MS_MAX        30000UL
#define IRRIGATE_MS_MIN            500UL
#define IRRIGATE_MS_MAX         120000UL
#define IRRIGATE_PAUSE_MS_MIN    60000UL  // Floor: pause is the only re-irrigation guard (diffusion time)
#define IRRIGATE_PAUSE_MS_MAX  3600000UL
#define SOIL_ADC_RAW_MAX           4095   // 12-bit ADC full scale
#define MAX_PLANTS                   50   // cap on stored plant entries
