#include "fileManager.h"
#include "constants.h"
#include <LittleFS.h>

// Internal result codes for logging
enum FSResult {
    FS_OK,
    FS_PARSE_ERROR,
    FS_CONFIG_INVALID,
    FS_NOT_FOUND,
    FS_BUSY,
    FS_WRITE_ERROR,
    FS_MOUNT_FAIL
};

static void applyDefaults(Config& cfg) {
    for (int i = 0; i < 5; i++) cfg.moisture[i] = DEFAULT_MOISTURE;

    cfg.tempTarget            = DEFAULT_TEMP_TARGET;
    cfg.tempHysteresis        = DEFAULT_TEMP_HYSTERESIS;
    cfg.luxTarget             = DEFAULT_LUX_TARGET;

    for (int h = 0; h < 24; h++) cfg.lightProfile[h] = 50; // safe default: 50% all day

    cfg.roofOpenDuration_ms   = DEFAULT_ROOF_OPEN_MS;
    cfg.roofCloseTimeout_ms   = DEFAULT_ROOF_CLOSE_TIMEOUT;
    cfg.irrigationDuration_ms = DEFAULT_IRRIGATE_MS;
    cfg.irrigationPause_ms    = DEFAULT_IRRIGATE_PAUSE;

    cfg.soilDryValue          = DEFAULT_SOIL_DRY;
    cfg.soilWetValue          = DEFAULT_SOIL_WET;

    cfg.configVersion         = CONFIG_VERSION;
}

static void configToJson(const Config& cfg, JsonDocument& doc) {
    JsonArray moisture = doc["moisture"].to<JsonArray>();
    for (int i = 0; i < 5; i++) moisture.add(cfg.moisture[i]);

    doc["tempTarget"]            = cfg.tempTarget;
    doc["tempHysteresis"]        = cfg.tempHysteresis;
    doc["luxTarget"]             = cfg.luxTarget;

    JsonArray profile = doc["lightProfile"].to<JsonArray>();
    for (int h = 0; h < 24; h++) profile.add(cfg.lightProfile[h]);

    doc["roofOpenDuration_ms"]   = cfg.roofOpenDuration_ms;
    doc["roofCloseTimeout_ms"]   = cfg.roofCloseTimeout_ms;
    doc["irrigationDuration_ms"] = cfg.irrigationDuration_ms;
    doc["irrigationPause_ms"]    = cfg.irrigationPause_ms;

    doc["soilDryValue"]          = cfg.soilDryValue;
    doc["soilWetValue"]          = cfg.soilWetValue;
    doc["configVersion"]         = cfg.configVersion;
}

static bool jsonToConfig(const JsonDocument& doc, Config& cfg) {
    if (!doc["configVersion"].is<int>()) return false;
    if ((int)doc["configVersion"] != CONFIG_VERSION) {
        Serial.println("[fileManager] config version mismatch — using defaults");
        return false;
    }

    JsonArrayConst moisture = doc["moisture"].as<JsonArrayConst>();
    if (moisture.size() < 5) return false;
    for (int i = 0; i < 5; i++) cfg.moisture[i] = moisture[i];

    cfg.tempTarget            = doc["tempTarget"]            | DEFAULT_TEMP_TARGET;
    cfg.tempHysteresis        = doc["tempHysteresis"]        | DEFAULT_TEMP_HYSTERESIS;
    cfg.luxTarget             = doc["luxTarget"]             | DEFAULT_LUX_TARGET;

    JsonArrayConst profile = doc["lightProfile"].as<JsonArrayConst>();
    if (profile.size() < 24) return false;
    for (int h = 0; h < 24; h++) cfg.lightProfile[h] = profile[h];

    cfg.roofOpenDuration_ms   = doc["roofOpenDuration_ms"]   | DEFAULT_ROOF_OPEN_MS;
    cfg.roofCloseTimeout_ms   = doc["roofCloseTimeout_ms"]   | DEFAULT_ROOF_CLOSE_TIMEOUT;
    cfg.irrigationDuration_ms = doc["irrigationDuration_ms"] | DEFAULT_IRRIGATE_MS;
    cfg.irrigationPause_ms    = doc["irrigationPause_ms"]    | DEFAULT_IRRIGATE_PAUSE;

    cfg.soilDryValue          = doc["soilDryValue"]          | DEFAULT_SOIL_DRY;
    cfg.soilWetValue          = doc["soilWetValue"]          | DEFAULT_SOIL_WET;
    cfg.configVersion         = CONFIG_VERSION;

    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool fileManager_init() {
    if (!LittleFS.begin(true)) {
        Serial.println("[fileManager] ERROR: LittleFS mount failed");
        return false;
    }

    // Basic health check: verify we can write a small test file
    File f = LittleFS.open("/.healthcheck", "w");
    if (!f) {
        Serial.println("[fileManager] ERROR: filesystem write test failed");
        return false;
    }
    f.print("ok");
    f.close();
    LittleFS.remove("/.healthcheck");

    Serial.println("[fileManager] init OK — free bytes: " +
                   String(LittleFS.totalBytes() - LittleFS.usedBytes()));
    return true;
}

bool loadConfig(Config& cfg) {
    if (!LittleFS.exists(CONFIG_PATH)) {
        Serial.println("[fileManager] config.json not found — using defaults");
        applyDefaults(cfg);
        return true; // not an error; defaults are valid
    }

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        Serial.println("[fileManager] ERROR: could not open config.json");
        applyDefaults(cfg);
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("[fileManager] ERROR: config.json parse failed — " + String(err.c_str()));
        applyDefaults(cfg);
        return false;
    }

    if (!jsonToConfig(doc, cfg)) {
        applyDefaults(cfg);
        return false;
    }

    Serial.println("[fileManager] config loaded OK");
    return true;
}

bool saveConfig(const Config& cfg) {
    backupConfig(); // rotate backups before overwriting

    JsonDocument doc;
    configToJson(cfg, doc);

    // Write to temp file first
    File tmp = LittleFS.open(CONFIG_TMP_PATH, "w");
    if (!tmp) {
        Serial.println("[fileManager] ERROR: could not open config.tmp for writing");
        return false;
    }
    serializeJsonPretty(doc, tmp);
    tmp.close();

    // Atomic rename: tmp → config.json
    LittleFS.remove(CONFIG_PATH);
    if (!LittleFS.rename(CONFIG_TMP_PATH, CONFIG_PATH)) {
        Serial.println("[fileManager] ERROR: rename config.tmp -> config.json failed");
        return false;
    }

    Serial.println("[fileManager] config saved OK");
    return true;
}

bool loadPlants(JsonDocument& doc) {
    if (!LittleFS.exists(PLANTS_PATH)) {
        doc.to<JsonArray>(); // return empty array
        return true;
    }

    File f = LittleFS.open(PLANTS_PATH, "r");
    if (!f) {
        Serial.println("[fileManager] ERROR: could not open plants.json");
        return false;
    }

    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("[fileManager] ERROR: plants.json parse failed — " + String(err.c_str()));
        return false;
    }

    return true;
}

bool savePlants(const JsonDocument& doc) {
    File f = LittleFS.open(PLANTS_PATH, "w");
    if (!f) {
        Serial.println("[fileManager] ERROR: could not open plants.json for writing");
        return false;
    }
    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[fileManager] plants saved OK");
    return true;
}

bool backupConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) return true; // nothing to back up

    // Rotate: bak2 → bak3, bak1 → bak2, config → bak1
    LittleFS.remove(CONFIG_BAK3_PATH);
    LittleFS.rename(CONFIG_BAK2_PATH, CONFIG_BAK3_PATH);
    LittleFS.rename(CONFIG_BAK1_PATH, CONFIG_BAK2_PATH);
    LittleFS.rename(CONFIG_PATH,      CONFIG_BAK1_PATH);

    Serial.println("[fileManager] backup rotated");
    return true;
}

bool restoreBackup(const String& filename) {
    if (!LittleFS.exists(filename)) {
        Serial.println("[fileManager] ERROR: backup not found — " + filename);
        return false;
    }

    LittleFS.remove(CONFIG_PATH);
    if (!LittleFS.rename(filename, CONFIG_PATH)) {
        Serial.println("[fileManager] ERROR: could not restore backup " + filename);
        return false;
    }

    Serial.println("[fileManager] restored backup " + filename);
    return true;
}
