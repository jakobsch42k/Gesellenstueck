#pragma once

#include <Arduino.h>
#include "constants.h"

struct LiveData; // forward decl — only timeOfDay is read; no full include needed

enum class LogLevel : uint8_t { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

struct LogEntry {
    uint32_t uptimeMs;    // millis() at log time — monotonic ordering
    uint32_t timeOfDay;   // seconds since midnight (0 until /setTime is called)
    LogLevel level;
    String   tag;
    String   msg;
};

// Cross-cutting event/error journal. A single global instance (declared at the
// bottom) lets any module log without threading a pointer through every init() —
// a deliberate exception to the pointer-passing convention used elsewhere.
//
// Sinks: Serial (always) and a rolling LittleFS file (INFO+ only). A RAM ring of
// the most recent entries backs /logs.json so HTTP handlers never read flash.
class Logger {
public:
    // Call once after LittleFS is mounted. fsReady=false keeps the journal in RAM
    // only (no persistence) so logging still works on a failed mount.
    void init(bool fsReady, const LiveData* liveForTime = nullptr);
    void setTimeSource(const LiveData* live) { _live = live; }

    void log(LogLevel level, const char* tag, const String& msg);
    void debug(const char* tag, const String& msg) { log(LogLevel::DEBUG, tag, msg); }
    void info (const char* tag, const String& msg) { log(LogLevel::INFO,  tag, msg); }
    void warn (const char* tag, const String& msg) { log(LogLevel::WARN,  tag, msg); }
    void error(const char* tag, const String& msg) { log(LogLevel::ERROR, tag, msg); }

    // Ring access for /logs.json — index 0 is oldest, count()-1 is newest.
    uint8_t         count() const { return _count; }
    const LogEntry& entry(uint8_t i) const;

    static const char* levelName(LogLevel l);

private:
    void pushRing(const LogEntry& e);
    void appendFile(const LogEntry& e);
    void rotate();
    void loadTail();

    LogEntry _ring[LOG_RING_SIZE];
    uint8_t  _head    = 0;     // next write slot
    uint8_t  _count   = 0;     // valid entries (<= LOG_RING_SIZE)
    bool     _fsReady = false;
    const LiveData* _live = nullptr;

    // Consecutive-duplicate suppression — stops a fault re-asserted every loop
    // pass from flooding flash and Serial.
    bool     _haveLast  = false;
    LogLevel _lastLevel = LogLevel::DEBUG;
    String   _lastTag;
    String   _lastMsg;
};

extern Logger logger;
