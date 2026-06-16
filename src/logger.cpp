#include "logger.h"
#include "shared.h"
#include <LittleFS.h>

Logger logger;

const char* Logger::levelName(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?";
}

// timeOfDay (seconds since midnight) → "HH:MM:SS". No date exists on the device.
static String fmtClock(uint32_t timeOfDay) {
    uint32_t s = timeOfDay % 86400UL;
    char buf[9];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s % 3600) / 60), (unsigned)(s % 60));
    return String(buf);
}

void Logger::init(bool fsReady, const LiveData* liveForTime) {
    _fsReady = fsReady;
    _live    = liveForTime;
    if (_fsReady) loadTail(); // seed the ring with recent history across reboots
}

const LogEntry& Logger::entry(uint8_t i) const {
    uint8_t start = (_count < LOG_RING_SIZE) ? 0 : _head;
    return _ring[(start + i) % LOG_RING_SIZE];
}

void Logger::pushRing(const LogEntry& e) {
    _ring[_head] = e;
    _head = (_head + 1) % LOG_RING_SIZE;
    if (_count < LOG_RING_SIZE) _count++;
}

void Logger::log(LogLevel level, const char* tag, const String& msg) {
    // Drop consecutive identical entries (same level+tag+msg).
    if (_haveLast && level == _lastLevel && _lastTag == tag && _lastMsg == msg) return;
    _haveLast = true; _lastLevel = level; _lastTag = tag; _lastMsg = msg;

    // Serial sink: always, preserving the existing "[tag] message" convention.
    Serial.println("[" + String(tag) + "] " + msg);

    // Journal sink: INFO and above only — keeps DEBUG chatter out of flash.
    if (level < LogLevel::INFO) return;

    LogEntry e;
    e.uptimeMs  = millis();
    e.timeOfDay = _live ? _live->timeOfDay : 0;
    e.level     = level;
    e.tag       = tag;
    e.msg       = msg;

    pushRing(e);
    if (_fsReady) appendFile(e);
}

void Logger::appendFile(const LogEntry& e) {
    // Rotate before appending once the active file has reached the cap.
    if (LittleFS.exists(LOG_PATH)) {
        File r = LittleFS.open(LOG_PATH, "r");
        uint32_t sz = r ? (uint32_t)r.size() : 0;
        r.close();
        if (sz > LOG_MAX_BYTES) rotate();
    }
    File f = LittleFS.open(LOG_PATH, "a");
    if (!f) return;
    // Line format: <HH:MM:SS>|<uptimeMs>|<LEVEL>|<tag>|<msg>
    f.print(fmtClock(e.timeOfDay));
    f.print('|'); f.print(e.uptimeMs);
    f.print('|'); f.print(levelName(e.level));
    f.print('|'); f.print(e.tag);
    f.print('|'); f.print(e.msg);
    f.print('\n');
    f.close();
}

void Logger::rotate() {
    // Same rolling-backup shape as fileManager::backupConfig().
    LittleFS.remove(LOG_BAK3_PATH);
    LittleFS.rename(LOG_BAK2_PATH, LOG_BAK3_PATH);
    LittleFS.rename(LOG_BAK1_PATH, LOG_BAK2_PATH);
    LittleFS.rename(LOG_PATH,      LOG_BAK1_PATH);
}

void Logger::loadTail() {
    if (!LittleFS.exists(LOG_PATH)) return;
    File f = LittleFS.open(LOG_PATH, "r");
    if (!f) return;
    // Sequential read; the ring naturally retains only the last LOG_RING_SIZE lines.
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        // Parse "<clock>|<uptime>|<LEVEL>|<tag>|<msg>"
        int p1 = line.indexOf('|');
        int p2 = line.indexOf('|', p1 + 1);
        int p3 = line.indexOf('|', p2 + 1);
        int p4 = line.indexOf('|', p3 + 1);
        if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) continue;

        LogEntry e;
        String clk = line.substring(0, p1);
        e.uptimeMs = (uint32_t)line.substring(p1 + 1, p2).toInt();
        String lvl = line.substring(p2 + 1, p3);
        e.tag      = line.substring(p3 + 1, p4);
        e.msg      = line.substring(p4 + 1);

        int c1 = clk.indexOf(':'), c2 = clk.indexOf(':', c1 + 1);
        e.timeOfDay = (c1 > 0 && c2 > 0)
            ? (uint32_t)(clk.substring(0, c1).toInt() * 3600L
                       + clk.substring(c1 + 1, c2).toInt() * 60L
                       + clk.substring(c2 + 1).toInt())
            : 0;

        e.level = LogLevel::INFO;
        if      (lvl == "WARN")  e.level = LogLevel::WARN;
        else if (lvl == "ERROR") e.level = LogLevel::ERROR;
        else if (lvl == "DEBUG") e.level = LogLevel::DEBUG;

        pushRing(e);
    }
    f.close();
}
