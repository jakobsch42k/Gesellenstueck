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

static FSResult tryLoadConfigFile(const char* path, Config& cfg) {
    if (!LittleFS.exists(path)) return FS_NOT_FOUND;

    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.println("[fileManager] ERROR: could not open " + String(path));
        return FS_NOT_FOUND;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("[fileManager] ERROR: " + String(path) + " parse failed — " + String(err.c_str()));
        return FS_PARSE_ERROR;
    }

    if (!jsonToConfig(doc, cfg)) return FS_CONFIG_INVALID;
    return FS_OK;
}

bool loadConfig(Config& cfg) {
    FSResult primary = tryLoadConfigFile(CONFIG_PATH, cfg);
    if (primary == FS_OK) {
        Serial.println("[fileManager] config loaded OK");
        return true;
    }

    // Fall back to the newest backup. Covers the power-loss window in
    // saveConfig between backup rotation (config.json → bak1) and the
    // tmp → config.json rename, plus ordinary corruption of config.json.
    FSResult backup = tryLoadConfigFile(CONFIG_BAK1_PATH, cfg);
    if (backup == FS_OK) {
        Serial.println("[fileManager] config restored from bak1");
        return true;
    }

    if (primary == FS_NOT_FOUND && backup == FS_NOT_FOUND) {
        Serial.println("[fileManager] config.json not found — using defaults");
        applyDefaults(cfg);
        return true; // not an error; fresh filesystem, defaults are valid
    }

    Serial.println("[fileManager] config and bak1 unusable — using defaults");
    applyDefaults(cfg);
    return false;
}

bool saveConfig(const Config& cfg) {
    JsonDocument doc;
    configToJson(cfg, doc);

    // 1. Write to temp file first and verify — config.json stays untouched
    //    until the new data is known-good on flash.
    File tmp = LittleFS.open(CONFIG_TMP_PATH, "w");
    if (!tmp) {
        Serial.println("[fileManager] ERROR: could not open config.tmp for writing");
        return false;
    }
    size_t written = serializeJson(doc, tmp);
    tmp.close();
    if (written == 0) {
        Serial.println("[fileManager] ERROR: config.tmp write produced 0 bytes");
        LittleFS.remove(CONFIG_TMP_PATH);
        return false;
    }

    // 2. Rotate backups only now (moves config.json → bak1). If power dies
    //    between here and the rename, loadConfig falls back to bak1.
    backupConfig();

    // 3. Atomic rename: tmp → config.json
    LittleFS.remove(CONFIG_PATH); // normally a no-op — rotation moved it away
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
    // Write to temp file first, then atomic rename — same pattern as saveConfig.
    // Prevents plants.json corruption on power loss mid-write.
    const char* tmpPath = "/plants.tmp";
    File tmp = LittleFS.open(tmpPath, "w");
    if (!tmp) {
        Serial.println("[fileManager] ERROR: could not open plants.tmp for writing");
        return false;
    }
    serializeJson(doc, tmp); // compact — nobody reads the file in place
    tmp.close();

    LittleFS.remove(PLANTS_PATH);
    if (!LittleFS.rename(tmpPath, PLANTS_PATH)) {
        Serial.println("[fileManager] ERROR: rename plants.tmp -> plants.json failed");
        return false;
    }
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
