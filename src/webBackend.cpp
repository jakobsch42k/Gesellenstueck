#include "webBackend.h"
#include "constants.h"
#include "fileManager.h"
#include "roofControl.h"
#include "irrigation.h"
#include "lightManagement.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static WebServer  server(80);
static DNSServer  dnsServer;
static WiFiServer httpsRedirect(443); // intercepts iOS HTTPS captive-portal probes

static const LiveData* liveData = nullptr;
static Config*         cfg      = nullptr;
static ErrorFlags*     errFlags = nullptr;

static std::function<void(String, int)>   _manualCtrlCb;
static std::function<void()>              _ackErrorsCb;
static std::function<void(unsigned long)> _setTimeCb;

// ── Helpers ───────────────────────────────────────────────────────────────────

static String getContentType(const String& path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css"))  return "text/css";
    if (path.endsWith(".js"))   return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png"))  return "image/png";
    if (path.endsWith(".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

static bool serveFile(const String& path) {
    String p = path;
    if (p.endsWith("/")) p += "index.html";
    if (!LittleFS.exists(p)) return false;
    File f = LittleFS.open(p, "r");
    server.streamFile(f, getContentType(p));
    f.close();
    return true;
}

// Returns false and sends 400 if body is missing or unparseable
static bool parseBody(JsonDocument& doc) {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return false;
    }
    DeserializationError e = deserializeJson(doc, server.arg("plain"));
    if (e) {
        server.send(400, "application/json", "{\"error\":\"JSON parse error\"}");
        return false;
    }
    return true;
}

// ── Config validation & update ────────────────────────────────────────────────

static bool validateConfig(const JsonDocument& doc, String& errMsg) {
    if (!doc["moisture"].isNull()) {
        JsonArrayConst m = doc["moisture"].as<JsonArrayConst>();
        if (m.size() < 5) { errMsg = "moisture must have 5 values"; return false; }
        for (int i = 0; i < 5; i++) {
            int v = m[i];
            if (v < 0 || v > 100) { errMsg = "moisture values must be 0-100"; return false; }
        }
    }
    if (!doc["tempTarget"].isNull()) {
        float t = doc["tempTarget"];
        if (t < 0 || t > 60) { errMsg = "tempTarget out of range (0-60)"; return false; }
    }
    if (!doc["tempHysteresis"].isNull()) {
        float h = doc["tempHysteresis"];
        if (h <= 0 || h > 10) { errMsg = "tempHysteresis out of range (0.1-10)"; return false; }
    }
    if (!doc["luxTarget"].isNull()) {
        int l = doc["luxTarget"];
        if (l < 0 || l > 100000) { errMsg = "luxTarget out of range"; return false; }
    }
    if (!doc["lightProfile"].isNull()) {
        JsonArrayConst p = doc["lightProfile"].as<JsonArrayConst>();
        if (p.size() < 24) { errMsg = "lightProfile must have 24 values"; return false; }
        for (int h = 0; h < 24; h++) {
            int v = p[h];
            if (v < 0 || v > 100) { errMsg = "lightProfile values must be 0-100"; return false; }
        }
    }
    return true;
}

static void applyJsonToConfig(const JsonDocument& doc, Config& c) {
    if (!doc["moisture"].isNull()) {
        JsonArrayConst m = doc["moisture"].as<JsonArrayConst>();
        for (int i = 0; i < 5; i++) c.moisture[i] = m[i];
    }
    if (!doc["tempTarget"].isNull())            c.tempTarget            = doc["tempTarget"];
    if (!doc["tempHysteresis"].isNull())        c.tempHysteresis        = doc["tempHysteresis"];
    if (!doc["luxTarget"].isNull())             c.luxTarget             = doc["luxTarget"];
    if (!doc["lightProfile"].isNull()) {
        JsonArrayConst p = doc["lightProfile"].as<JsonArrayConst>();
        for (int h = 0; h < 24; h++) c.lightProfile[h] = p[h];
    }
    if (!doc["roofOpenDuration_ms"].isNull())   c.roofOpenDuration_ms   = doc["roofOpenDuration_ms"];
    if (!doc["roofCloseTimeout_ms"].isNull())   c.roofCloseTimeout_ms   = doc["roofCloseTimeout_ms"];
    if (!doc["irrigationDuration_ms"].isNull()) c.irrigationDuration_ms = doc["irrigationDuration_ms"];
    if (!doc["irrigationPause_ms"].isNull())    c.irrigationPause_ms    = doc["irrigationPause_ms"];
    if (!doc["soilDryValue"].isNull())          c.soilDryValue          = doc["soilDryValue"];
    if (!doc["soilWetValue"].isNull())          c.soilWetValue          = doc["soilWetValue"];
}

// ── Route handlers ────────────────────────────────────────────────────────────

static void handleData() {
    JsonDocument doc;
    doc["tempC"]       = liveData->tempC;
    doc["humPerc"]     = liveData->humPerc;
    doc["lux"]         = liveData->lux;
    doc["luxSmoothed"] = liveData->luxSmoothed;

    JsonArray soil = doc["soilPerc"].to<JsonArray>();
    for (int i = 0; i < 5; i++) soil.add(liveData->soilPerc[i]);

    doc["waterLow"]      = liveData->waterLow;
    doc["waterCritical"] = liveData->waterCritical;
    doc["roofClosed"]    = liveData->roofClosed;
    doc["timeOfDay"]     = liveData->timeOfDay;

    const char* irrStr[] = {"IDLE", "PUMPING", "PAUSING"};
    doc["pumpStatus"]    = irrStr[(int)irrigation_getState()];

    doc["ERR_ROOF_TIMEOUT"]   = errFlags->ERR_ROOF_TIMEOUT;
    doc["ERR_WATER_CRITICAL"] = errFlags->ERR_WATER_CRITICAL;
    doc["ERR_FS_MOUNT"]       = errFlags->ERR_FS_MOUNT;
    doc["EMERGENCY_STOP"]     = errFlags->EMERGENCY_STOP;
    doc["MAINTENANCE_MODE"]   = errFlags->MAINTENANCE_MODE;
    doc["ERR_SENSOR_BME"]     = errFlags->ERR_SENSOR_BME;
    doc["ERR_SENSOR_BH"]      = errFlags->ERR_SENSOR_BH;
    doc["lastErrorMessage"]   = errFlags->lastErrorMessage;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

static void handleSystemStatus() {
    JsonDocument doc;

    unsigned long upSec = millis() / 1000;
    char upStr[32];
    snprintf(upStr, sizeof(upStr), "%lud %02luh %02lum %02lus",
             upSec / 86400, (upSec % 86400) / 3600,
             (upSec % 3600) / 60, upSec % 60);
    doc["uptime"]   = upStr;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["cpuFreq"]  = ESP.getCpuFreqMHz();

    if (WiFi.getMode() == WIFI_AP) {
        doc["wifi"] = "AP — " + String(WiFi.softAPgetStationNum()) + " client(s)";
        doc["ip"]   = WiFi.softAPIP().toString();
    } else {
        doc["wifi"] = WiFi.SSID();
        doc["ip"]   = WiFi.localIP().toString();
    }

    doc["ERR_ROOF_TIMEOUT"]   = errFlags->ERR_ROOF_TIMEOUT;
    doc["ERR_WATER_CRITICAL"] = errFlags->ERR_WATER_CRITICAL;
    doc["ERR_FS_MOUNT"]       = errFlags->ERR_FS_MOUNT;
    doc["EMERGENCY_STOP"]     = errFlags->EMERGENCY_STOP;
    doc["ERR_SENSOR_BME"]     = errFlags->ERR_SENSOR_BME;
    doc["ERR_SENSOR_BH"]      = errFlags->ERR_SENSOR_BH;
    doc["lastErrorMessage"]   = errFlags->lastErrorMessage;

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

static void handleLoadConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) {
        server.send(500, "application/json", "{\"error\":\"config.json not found\"}");
        return;
    }
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        server.send(500, "application/json", "{\"error\":\"could not open config.json\"}");
        return;
    }
    server.streamFile(f, "application/json");
    f.close();
}

static void handleSaveConfig() {
    if (errFlags->ERR_FS_MOUNT) {
        server.send(503, "application/json", "{\"error\":\"filesystem unavailable\"}");
        return;
    }
    JsonDocument doc;
    if (!parseBody(doc)) return;

    String errMsg;
    if (!validateConfig(doc, errMsg)) {
        server.send(400, "application/json", "{\"error\":\"" + errMsg + "\"}");
        return;
    }

    applyJsonToConfig(doc, *cfg);
    if (!saveConfig(*cfg)) {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleGetPlants() {
    if (!LittleFS.exists(PLANTS_PATH)) {
        server.send(200, "application/json", "[]");
        return;
    }
    File f = LittleFS.open(PLANTS_PATH, "r");
    if (!f) {
        server.send(500, "application/json", "{\"error\":\"could not open plants.json\"}");
        return;
    }
    server.streamFile(f, "application/json");
    f.close();
}

static void handleAddPlant() {
    JsonDocument newPlant;
    if (!parseBody(newPlant)) return;

    if (newPlant["name"].isNull()) {
        server.send(400, "application/json", "{\"error\":\"name required\"}");
        return;
    }

    JsonDocument doc;
    loadPlants(doc);
    JsonArray arr = doc.as<JsonArray>();
    if (!arr) arr = doc.to<JsonArray>();

    String newName = newPlant["name"].as<String>();
    for (JsonVariant p : arr) {
        if (p["name"].as<String>() == newName) {
            server.send(409, "application/json", "{\"error\":\"plant already exists\"}");
            return;
        }
    }

    arr.add(newPlant);
    if (!savePlants(doc)) {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleDeletePlant() {
    JsonDocument reqDoc;
    if (!parseBody(reqDoc)) return;

    if (reqDoc["name"].isNull()) {
        server.send(400, "application/json", "{\"error\":\"name required\"}");
        return;
    }

    JsonDocument doc;
    loadPlants(doc);
    JsonArray arr = doc.as<JsonArray>();
    if (!arr) {
        server.send(404, "application/json", "{\"error\":\"plant not found\"}");
        return;
    }

    String nameToDelete = reqDoc["name"].as<String>();
    JsonDocument newDoc;
    JsonArray newArr = newDoc.to<JsonArray>();
    bool found = false;

    for (JsonVariant p : arr) {
        if (p["name"].as<String>() == nameToDelete) {
            found = true;
        } else {
            newArr.add(p);
        }
    }

    if (!found) {
        server.send(404, "application/json", "{\"error\":\"plant not found\"}");
        return;
    }

    if (!savePlants(newDoc)) {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleManual() {
    JsonDocument doc;
    if (!parseBody(doc)) return;

    if (doc["command"].isNull()) {
        server.send(400, "application/json", "{\"error\":\"command required\"}");
        return;
    }

    String cmd = doc["command"].as<String>();
    int val    = doc["value"] | 0;

    if (_manualCtrlCb) _manualCtrlCb(cmd, val);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleDiagnostics() {
    JsonDocument doc;

    // Sensor raw values
    JsonArray rawSoil = doc["soilRaw"].to<JsonArray>();
    for (int i = 0; i < 5; i++) rawSoil.add(liveData->soilRaw[i]);

    doc["temperature"] = liveData->tempC;
    doc["humidity"]    = liveData->humPerc;
    doc["light"]       = liveData->lux;

    doc["waterLow"]      = liveData->waterLow      ? "OK" : "Low";
    doc["waterCritical"] = liveData->waterCritical ? "OK" : "CRITICAL";

    // System states
    doc["roofContact"] = liveData->roofClosed ? "Closed" : "Open";

    const char* irrStr[] = {"IDLE", "PUMPING", "PAUSING"};
    doc["pumpStatus"] = irrStr[(int)irrigation_getState()];

    // Active error flags as a comma-separated string
    String flags = "";
    if (errFlags->EMERGENCY_STOP)     flags += "EMERGENCY_STOP, ";
    if (errFlags->ERR_WATER_CRITICAL) flags += "WATER_CRITICAL, ";
    if (errFlags->ERR_ROOF_TIMEOUT)   flags += "ROOF_TIMEOUT, ";
    if (errFlags->ERR_FS_MOUNT)       flags += "FS_MOUNT, ";
    if (errFlags->ERR_SENSOR_BME)     flags += "NO_BME, ";
    if (errFlags->ERR_SENSOR_BH)      flags += "NO_BH, ";
    for (int i = 0; i < 5; i++) {
        if (errFlags->ERR_SOIL[i]) flags += "SOIL" + String(i + 1) + ", ";
    }
    if (flags.length() > 2) flags = flags.substring(0, flags.length() - 2);
    doc["errorFlags"] = flags.length() ? flags : "None";

    // System information
    if (WiFi.getMode() == WIFI_AP)
        doc["commStatus"] = "AP — " + String(WiFi.softAPgetStationNum()) + " client(s), IP " + WiFi.softAPIP().toString();
    else
        doc["commStatus"] = WiFi.SSID() + " — " + WiFi.localIP().toString();

    const char* roofStr[] = {"IDLE", "OPENING", "OPEN", "CLOSING", "CLOSED", "ERROR"};
    doc["moduleStatus"] = String("Roof: ") + roofStr[(int)roofControl_getState()] +
                          " | Irr: " + irrStr[(int)irrigation_getState()] +
                          " | LED PWM: " + String(lightManagement_getCurrentPWM());

    doc["voltage"]  = "N/A";
    doc["freeHeap"] = ESP.getFreeHeap();

    JsonArray errSoil = doc["ERR_SOIL"].to<JsonArray>();
    for (int i = 0; i < 5; i++) errSoil.add(errFlags->ERR_SOIL[i]);

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
}

static void handleSetTime() {
    JsonDocument doc;
    if (!parseBody(doc)) return;

    if (doc["timestamp"].isNull()) {
        server.send(400, "application/json", "{\"error\":\"timestamp required\"}");
        return;
    }

    unsigned long ts = doc["timestamp"].as<unsigned long>();
    if (_setTimeCb) _setTimeCb(ts);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleReboot() {
    server.send(202, "application/json", "{\"status\":\"rebooting\"}");
    delay(200);
    ESP.restart();
}

static void handleExportConfig() {
    if (!LittleFS.exists(CONFIG_PATH)) {
        server.send(500, "application/json", "{\"error\":\"config.json not found\"}");
        return;
    }
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        server.send(500, "application/json", "{\"error\":\"could not open config.json\"}");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"config.json\"");
    server.streamFile(f, "application/json");
    f.close();
}

static void handleImportConfig() {
    if (errFlags->ERR_FS_MOUNT) {
        server.send(503, "application/json", "{\"error\":\"filesystem unavailable\"}");
        return;
    }
    JsonDocument doc;
    if (!parseBody(doc)) return;

    String errMsg;
    if (!validateConfig(doc, errMsg)) {
        server.send(400, "application/json", "{\"error\":\"" + errMsg + "\"}");
        return;
    }

    applyJsonToConfig(doc, *cfg);
    if (!saveConfig(*cfg)) {
        server.send(500, "application/json", "{\"error\":\"save failed\"}");
        return;
    }
    server.send(200, "application/json", "{\"status\":\"imported\"}");
}

static void handleAckErrors() {
    if (_ackErrorsCb) _ackErrorsCb();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleNotFound() {
    if (!serveFile(server.uri())) {
        // Redirect unknown paths to the main page — triggers captive portal on all OSes
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void webBackend_init(const LiveData& data, Config& config, ErrorFlags& err) {
    liveData = &data;
    cfg      = &config;
    errFlags = &err;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASS, 6);
    Serial.println("[webBackend] AP started — IP: " + WiFi.softAPIP().toString());

    // Captive portal DNS: resolve every hostname to the ESP32 IP
    dnsServer.start(53, "*", WiFi.softAPIP());
    httpsRedirect.begin();

    // OS captive-portal detection probes — all redirect so the browser popup triggers
    auto captiveRedirect = []() {
        server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        server.send(302, "text/plain", "");
    };
    server.on("/generate_204",        HTTP_GET, captiveRedirect); // Android
    server.on("/204",                 HTTP_GET, captiveRedirect); // Android (alternate)
    server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect); // iOS / macOS
    server.on("/canonical.html",      HTTP_GET, captiveRedirect); // iOS (alternate)
    server.on("/connecttest.txt",     HTTP_GET, captiveRedirect); // Windows
    server.on("/redirect",            HTTP_GET, captiveRedirect); // Windows
    server.on("/ncsi.txt",            HTTP_GET, captiveRedirect); // Windows (older)
    server.on("/success.html",        HTTP_GET, captiveRedirect); // macOS (alternate)

    server.on("/",            HTTP_GET, []() { serveFile("/index.html"); });
    server.on("/index.html",  HTTP_GET, []() { serveFile("/index.html"); });
    server.on("/style.css",   HTTP_GET, []() { serveFile("/style.css");  });
    server.on("/script.js",   HTTP_GET, []() { serveFile("/script.js");  });

    server.on("/data.json",    HTTP_GET,    handleData);
    server.on("/systemStatus", HTTP_GET,    handleSystemStatus);
    server.on("/loadConfig",   HTTP_GET,    handleLoadConfig);
    server.on("/saveConfig",   HTTP_POST,   handleSaveConfig);
    server.on("/getPlants",    HTTP_GET,    handleGetPlants);
    server.on("/addPlant",     HTTP_POST,   handleAddPlant);
    server.on("/deletePlant",  HTTP_DELETE, handleDeletePlant);
    server.on("/manual",       HTTP_POST,   handleManual);
    server.on("/diagnostics",  HTTP_GET,    handleDiagnostics);
    server.on("/setTime",      HTTP_POST,   handleSetTime);
    server.on("/reboot",       HTTP_POST,   handleReboot);
    server.on("/exportConfig", HTTP_GET,    handleExportConfig);
    server.on("/importConfig", HTTP_POST,   handleImportConfig);
    server.on("/ackErrors",    HTTP_POST,   handleAckErrors);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("[webBackend] HTTP server started");
}

void webBackend_registerCallbacks(
    std::function<void(String, int)>   manualCtrlCb,
    std::function<void()>              ackErrorsCb,
    std::function<void(unsigned long)> setTimeCb)
{
    _manualCtrlCb = manualCtrlCb;
    _ackErrorsCb  = ackErrorsCb;
    _setTimeCb    = setTimeCb;
}

void webBackend_handle() {
    dnsServer.processNextRequest();
    server.handleClient();

    // Send plaintext HTTP redirect to any iOS HTTPS probe on port 443
    WiFiClient client = httpsRedirect.accept();
    if (client) {
        String redirect = "HTTP/1.1 302 Found\r\nLocation: http://" +
                          WiFi.softAPIP().toString() + "/\r\n"
                          "Content-Length: 0\r\nConnection: close\r\n\r\n";
        client.print(redirect);
        client.stop();
    }
}
