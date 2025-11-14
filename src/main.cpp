/*
 * ESP32 Plant Monitoring Web Server
 *
 * This program sets up an ESP32 as a WiFi access point and web server to monitor
 * environmental conditions for plant growth. It uses sensors to measure light,
 * temperature, and humidity, and provides a web interface for configuration
 * and plant management.
 *
 * Hardware:
 * - ESP32 microcontroller
 * - BH1750 light sensor
 * - BME280 temperature and humidity sensor
 * - Moisture sensors (up to 5)
 *
 * Features:
 * - WiFi AP mode for local access
 * - Web server serving static files from LittleFS
 * - REST API endpoints for data retrieval and configuration
 * - Plant database management
 * - Configuration persistence in JSON files
 */

#include <Arduino.h>
#include <BH1750.h>
#include <Adafruit_BME280.h>

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// WiFi Access Point credentials
const char* ssid = "Esp-32-Webserver";
const char* password = "12345678";

// Web server instance on port 80
WebServer server(80);

// Configuration structure for moisture targets and light threshold
struct Config {
  int moisture[5];  // Target moisture levels for 5 plant beds (0-100%)
  int lux;          // Target light level in lux
} config;

// Global variables for sensor readings
float temperature = 0.0;  // Current temperature in °C
float humidity = 0.0;     // Current humidity in %
float light = 0.0;        // Current light level in lux

// Helper function to determine MIME type based on file extension
String getContentType(const String& filename) {
  if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";  // HTML files
  else if (filename.endsWith(".css")) return "text/css";                           // CSS stylesheets
  else if (filename.endsWith(".js")) return "application/javascript";             // JavaScript files
  else if (filename.endsWith(".png")) return "image/png";                         // PNG images
  else if (filename.endsWith(".gif")) return "image/gif";                         // GIF images
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";  // JPEG images
  else if (filename.endsWith(".ico")) return "image/x-icon";                      // Icon files
  else if (filename.endsWith(".svg")) return "image/svg+xml";                     // SVG vector graphics
  else if (filename.endsWith(".json")) return "application/json";                 // JSON data files
  else if (filename.endsWith(".txt")) return "text/plain";                        // Plain text files
  return "application/octet-stream";                                              // Default for unknown types
}

// Function to serve files from LittleFS filesystem
bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";  // If requesting a directory, serve index.html as default
  if (!LittleFS.exists(path)) {                   // Check if the requested file exists in LittleFS
    Serial.println("Datei nicht gefunden: " + path);
    return false;                                // Return false if file not found
  }

  File file = LittleFS.open(path, "r");           // Open the file in read mode
  String contentType = getContentType(path);     // Determine the appropriate MIME type
  server.streamFile(file, contentType);          // Stream the file content to the client
  file.close();                                  // Close the file to free resources
  return true;                                   // Return true on successful file serving
}

// Default handler for all incoming requests - serves files or returns 404
void handleNotFound() {
  if (!handleFileRead(server.uri())) {           // Try to serve the requested URI as a file
    server.send(404, "text/plain", "404: Datei nicht gefunden");  // If file not found, send 404 error
  }
}

// API endpoint handler for /data.json - returns current sensor readings
void handleData() {
  JsonDocument doc;                              // Create a JSON document with capacity for sensor data
  doc["light"] = round(light *100)/100;          // Round light value to 2 decimal places
  doc["temperature"] = round(temperature *100)/100;  // Round temperature to 2 decimal places
  doc["humidity"] = round(humidity *100)/100;    // Round humidity to 2 decimal places
  String json;                                   // String to hold the serialized JSON
  serializeJson(doc, json);                      // Convert JSON document to string
  server.send(200, "application/json", json);    // Send JSON response with HTTP 200 OK
}

// Load configuration from LittleFS config.json file
void loadConfig() {
  if (!LittleFS.exists("/config.json")) {        // Check if configuration file exists
    Serial.println("Keine config.json gefunden, Standardwerte");
    for (int i = 0; i < 5; i++) config.moisture[i] = 40 + i*10;  // Set default moisture levels (40%, 50%, 60%, 70%, 80%)
    config.lux = 150;                            // Set default light threshold
    return;                                      // Exit function with defaults
  }

  File file = LittleFS.open("/config.json", "r"); // Open config file for reading
  JsonDocument doc;                             // JSON document to parse the file
  DeserializationError err = deserializeJson(doc, file);  // Parse JSON from file
  file.close();                                  // Close the file
  if (err) {                                     // If parsing failed
    Serial.println("Fehler beim Laden von config.json");
    return;                                      // Exit without loading
  }

  for (int i = 0; i < 5; i++) config.moisture[i] = doc["moisture" + String(i+1)];  // Load moisture settings
  config.lux = doc["lux"];                       // Load light threshold
  Serial.println("Config geladen");              // Confirm successful loading
}

// Save current configuration to LittleFS config.json file
void saveConfig() {
  JsonDocument doc;                              // JSON document to store configuration
  for (int i = 0; i < 5; i++) doc["moisture" + String(i+1)] = config.moisture[i];  // Add moisture settings
  doc["lux"] = config.lux;                       // Add light threshold

  File file = LittleFS.open("/config.json", "w"); // Open config file for writing (creates if doesn't exist)
  if (!file) {                                   // If file couldn't be opened
    Serial.println("Konnte config.json nicht öffnen");
    return;                                      // Exit without saving
  }
  serializeJsonPretty(doc, file);                // Write formatted JSON to file
  file.close();                                  // Close the file
  Serial.println("Config gespeichert");          // Confirm successful saving
}

// API endpoint handler for /loadConfig - returns current configuration
void handleGetConfig() {
  JsonDocument doc;                              // JSON document to build response
  for (int i = 0; i < 5; i++) doc["moisture" + String(i+1)] = config.moisture[i];  // Add current moisture settings
  doc["lux"] = config.lux;                       // Add current light threshold

  String json;                                   // String to hold serialized JSON
  serializeJson(doc, json);                      // Convert to JSON string
  server.send(200, "application/json", json);    // Send configuration as JSON response
}

// API endpoint handler for POST /saveConfig - updates and saves configuration
void handleSetConfig() {
  if (server.hasArg("plain") == false) {         // Check if request body is present
    server.send(400, "text/plain", "Kein Body empfangen");  // Send 400 Bad Request if no body
    return;
  }

  JsonDocument doc;                             // JSON document to parse request
  DeserializationError error = deserializeJson(doc, server.arg("plain"));  // Parse JSON from request body
  if (error) {                                   // If parsing failed
    server.send(400, "text/plain", "JSON Fehler");  // Send 400 Bad Request for invalid JSON
    return;
  }

  for (int i = 0; i < 5; i++) config.moisture[i] = doc["moisture" + String(i+1)];  // Update moisture settings
  config.lux = doc["lux"];                       // Update light threshold
  saveConfig();                                  // Save updated configuration to file
  server.send(200, "text/plain", "OK");          // Send success response
}

// API endpoint handler for /getPlants - returns plant database
void handleGetPlants() {
  if (!LittleFS.exists("/plants.json")) {        // Check if plant database file exists
    server.send(200, "application/json", "[]");  // If not, return empty array
    return;
  }
  File file = LittleFS.open("/plants.json", "r"); // Open plant database file
  String data = file.readString();               // Read entire file content
  file.close();                                  // Close the file
  server.send(200, "application/json", data);    // Send plant data as JSON response
}

// API endpoint handler for POST /addPlant - adds a new plant to the database
void handleAddPlant() {
  if (!server.hasArg("plain")) {                  // Check if request body is present
    server.send(400, "text/plain", "Kein Body empfangen");  // Send 400 if no body
    return;
  }

  // Load existing plants from file
  JsonDocument doc;                               // JSON document for plant database
  if (LittleFS.exists("/plants.json")) {         // If plant file exists
    File file = LittleFS.open("/plants.json", "r");  // Open for reading
    DeserializationError err = deserializeJson(doc, file);  // Parse existing data
    file.close();                                // Close file
    if (err) doc.clear();                        // Clear document if parsing failed
  }

  JsonArray arr = doc.as<JsonArray>();           // Get or create JSON array
  if (!arr) arr = doc.to<JsonArray>();           // Ensure it's an array

  JsonDocument newPlant;                          // Document for new plant data
  DeserializationError err = deserializeJson(newPlant, server.arg("plain"));  // Parse new plant from request
  if (err) {                                     // If parsing failed
    server.send(400, "text/plain", "JSON Fehler");  // Send 400 for invalid JSON
    return;
  }

  // Check for duplicate plant names before adding
  String newPlantName = newPlant["name"];         // Get the name of the new plant
  for (JsonVariant plant : arr) {                 // Iterate through existing plants
    if (plant["name"] == newPlantName) {          // If name already exists
      server.send(409, "text/plain", "Pflanze mit diesem Namen existiert bereits");  // Send 409 Conflict
      return;
    }
  }

  arr.add(newPlant);                             // Add new plant to array (no duplicate found)

  File file = LittleFS.open("/plants.json", "w"); // Open file for writing
  if (!file) {                                   // If couldn't open
    server.send(500, "text/plain", "Konnte plants.json nicht öffnen");  // Send 500 error
    return;
  }
  serializeJsonPretty(doc, file);                // Write updated database
  file.close();                                  // Close file
  server.send(200, "text/plain", "OK");          // Send success response
}

// API endpoint handler for DELETE /deletePlant - removes a plant from the database
void handleDeletePlant() {
  if (!server.hasArg("plain")) {                  // Check if request body is present
    server.send(400, "text/plain", "Kein Body empfangen");  // Send 400 if no body
    return;
  }

  // Load existing plants from file
  JsonDocument doc;                               // JSON document for plant database
  if (LittleFS.exists("/plants.json")) {         // If plant file exists
    File file = LittleFS.open("/plants.json", "r");  // Open for reading
    DeserializationError err = deserializeJson(doc, file);  // Parse existing data
    file.close();                                // Close file
    if (err) doc.clear();                        // Clear document if parsing failed
  }

  JsonArray arr = doc.as<JsonArray>();           // Get or create JSON array
  if (!arr) arr = doc.to<JsonArray>();           // Ensure it's an array

  JsonDocument requestData;                       // Document for request data
  DeserializationError err = deserializeJson(requestData, server.arg("plain"));  // Parse request
  if (err) {                                     // If parsing failed
    server.send(400, "text/plain", "JSON Fehler");  // Send 400 for invalid JSON
    return;
  }

  String plantNameToDelete = requestData["name"]; // Get name of plant to delete
  bool plantFound = false;                        // Flag to track if plant was found

  // Create new array without the plant to delete
  JsonDocument newDoc;                            // New document for filtered plants
  JsonArray newArr = newDoc.to<JsonArray>();

  for (JsonVariant plant : arr) {                 // Iterate through existing plants
    String plantName = plant["name"].as<String>();  // Get plant name as string
    if (plantName != plantNameToDelete) {        // Keep plants that don't match
      newArr.add(plant);
    } else {
      plantFound = true;                          // Mark that we found the plant to delete
    }
  }

  if (!plantFound) {                              // If plant was not found
    server.send(404, "text/plain", "Pflanze nicht gefunden");  // Send 404 Not Found
    return;
  }

  // Save updated plant database
  File file = LittleFS.open("/plants.json", "w"); // Open file for writing
  if (!file) {                                   // If couldn't open
    server.send(500, "text/plain", "Konnte plants.json nicht öffnen");  // Send 500 error
    return;
  }
  serializeJsonPretty(newDoc, file);             // Write updated database
  file.close();                                  // Close file
  server.send(200, "text/plain", "OK");          // Send success response
}

// API endpoint handler for /systemStatus - returns system status information
void handleSystemStatus() {
  JsonDocument doc;                               // Create JSON document for system status

  // Calculate uptime in seconds
  unsigned long uptimeSeconds = millis() / 1000;

  // Calculate uptime components
  unsigned long days = uptimeSeconds / 86400;
  unsigned long hours = (uptimeSeconds % 86400) / 3600;
  unsigned long minutes = (uptimeSeconds % 3600) / 60;
  unsigned long seconds = uptimeSeconds % 60;

  // Format uptime string
  String uptimeStr = "";
  if (days > 0) uptimeStr += String(days) + "d ";
  if (hours > 0 || days > 0) uptimeStr += String(hours) + "h ";
  if (minutes > 0 || hours > 0 || days > 0) uptimeStr += String(minutes) + "m ";
  uptimeStr += String(seconds) + "s";

  // Get WiFi status information
  String wifiStatus = "Nicht verbunden";
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = "Verbunden - " + WiFi.SSID() + " (" + String(WiFi.RSSI()) + " dBm)";
  } else if (WiFi.getMode() == WIFI_AP) {
    wifiStatus = "Access Point - " + String(WiFi.softAPgetStationNum()) + " Clients";
  }

  // Add data to JSON document
  doc["uptime"] = uptimeStr;
  doc["wifiStatus"] = wifiStatus;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["cpuFreq"] = ESP.getCpuFreqMHz();

  String json;                                   // String to hold the serialized JSON
  serializeJson(doc, json);                      // Convert JSON document to string
  server.send(200, "application/json", json);    // Send JSON response with HTTP 200 OK
}

// Arduino setup function - initializes hardware and starts services
void setup() {
  Serial.begin(115200);                          // Initialize serial communication at 115200 baud
  delay(500);                                    // Short delay for serial initialization

  // Set up WiFi Access Point
  WiFi.mode(WIFI_AP);                            // Set ESP32 to Access Point mode
  WiFi.softAP(ssid, password, 6);                // Start AP with credentials and channel 6 (iPhone compatible)

  IPAddress IP = WiFi.softAPIP();                // Get the AP's IP address
  Serial.print("AP gestartet! SSID: ");
  Serial.println(ssid);
  Serial.print("IP-Adresse: ");
  Serial.println(IP);

  Serial.println();
  Serial.print("Verbunden! IP-Adresse: ");
  Serial.println(WiFi.localIP());                // Note: In AP mode, this shows the same IP

  // Initialize LittleFS filesystem
  if (!LittleFS.begin(true)) {                    // Mount LittleFS, format if needed
    Serial.println("Fehler beim Mounten von LittleFS!");
    return;                                      // Stop setup if filesystem fails
  }

  // Set up web server routes
  server.on("/data.json", handleData);           // Route for sensor data
  server.on("/loadConfig", handleGetConfig);     // Route for getting configuration
  server.on("/saveConfig", HTTP_POST, handleSetConfig);  // Route for saving configuration
  server.on("/getPlants", handleGetPlants);      // Route for getting plant database
  server.on("/addPlant", HTTP_POST, handleAddPlant);  // Route for adding plants
  server.on("/deletePlant", HTTP_DELETE, handleDeletePlant);  // Route for deleting plants
  server.on("/systemStatus", handleSystemStatus);  // Route for system status
  server.onNotFound(handleNotFound);             // Default handler for other requests

  Serial.println("HTTP-Server gestartet");

  server.begin();                                // Start the web server

  // Load configuration from file
  loadConfig();                                  // Initialize config with saved or default values
}

// Arduino main loop - handles incoming web server requests
void loop() {
  server.handleClient();                          // Process any incoming HTTP requests
}