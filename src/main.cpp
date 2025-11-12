#include <Arduino.h>
#include <BH1750.h>
#include <Adafruit_BME280.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <ArduinoJson.h>


// --- HTML-Seite ---

#include "./html.cpp"

// --- Sensor Objekte ---
BH1750 lightSensor; // I2C
Adafruit_BME280 bme; // I2C

// --- WLAN-Access-Point ---
const char* ssid = "ESP32_WebServer";   // Name des WLANs
const char* password = "12345678";      // Passwort (min. 8 Zeichen)

// --- Webserver auf Port 80 ---
WebServer server(80);


// --- Sensor Variablen ---
float light;
float temperature;
float humidity;

// --- Handler für die Root-Seite ---
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// --- Handler für Sensordaten im JSON-Format ---
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
  delay(1000);
  Serial.println("Starte ESP32 Access Point...");

  // Access Point starten
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password, 6);  // Kanal 6 für iPhone-Kompatibilität

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP gestartet! SSID: ");
  Serial.println(ssid);
  Serial.print("IP-Adresse: ");
  Serial.println(IP);

  // Webserver starten
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Webserver gestartet!");

  Wire.begin(8, 9);


  // BH1750 initialisieren
  if(!lightSensor.begin()) {
    Serial.println("BH1750 nicht gefunden!");
  } else {
    Serial.println("BH1750 erfolgreich gestartet");
  }

  if(!bme.begin(0x76)) {  
    Serial.println("BME280 nicht gefunden!");
  } else {
    Serial.println("BME280 erfolgreich gestartet");
  }
}

void loop() {
  server.handleClient(); // Anfragen verarbeiten
  light = lightSensor.readLightLevel();
  temperature = bme.readTemperature();
  humidity = bme.readHumidity();

}
