#pragma once

#include "shared.h"
#include <ArduinoJson.h>

bool fileManager_init();
bool loadConfig(Config& cfg);
bool saveConfig(const Config& cfg);
bool loadPlants(JsonDocument& doc);
bool savePlants(const JsonDocument& doc);
bool backupConfig();
bool restoreBackup(const String& filename);
