#include <Arduino.h>
#include <BH1750.h>
#include <Adafruit_BME280.h>

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// WLAN-Zugangsdaten
const char* ssid = "Esp-32-Webserver";
const char* password = "12345678";

// Webserver auf Port 80
WebServer server(80);

// Tempdata
float temperature = 0.0;
float humidity = 0.0;
float light = 0.0;

// Hilfsfunktion zur automatischen MIME-Typ-Erkennung
String getContentType(const String& filename) {
  if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".svg")) return "image/svg+xml";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".txt")) return "text/plain";
  return "application/octet-stream";
}

// Funktion zum Senden einer Datei aus LittleFS
bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html"; // Default-Seite
  if (!LittleFS.exists(path)) {
    Serial.println("Datei nicht gefunden: " + path);
    return false;
  }

  File file = LittleFS.open(path, "r");
  String contentType = getContentType(path);
  server.streamFile(file, contentType);
  file.close();
  return true;
}

//Standard-Handler für alle eingehenden Anfragen
void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "404: Datei nicht gefunden");
  }
}

void handleData() {
  DynamicJsonDocument doc(256);
  doc["light"] = round(light *100)/100;
  doc["temperature"] = round(temperature *100)/100;
  doc["humidity"] = round(humidity *100)/100;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // WLAN verbinden
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 6);  // Kanal 6 für iPhone-Kompatibilität

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP gestartet! SSID: ");
  Serial.println(ssid);
  Serial.print("IP-Adresse: ");
  Serial.println(IP);


  Serial.println();
  Serial.print("Verbunden! IP-Adresse: ");
  Serial.println(WiFi.localIP());

  //LittleFS starten
  if (!LittleFS.begin(true)) {
    Serial.println("Fehler beim Mounten von LittleFS!");
    return;
  }

  //Standardrouten
  server.on("/data.json", handleData);
  server.onNotFound(handleNotFound);
  server.begin();
  

  Serial.println("HTTP-Server gestartet");
}

void loop() {
  server.handleClient();
}