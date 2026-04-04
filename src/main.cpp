#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <WebServer.h>

// --- Supabase Configuration ---
const String SUPABASE_URL = "https://akvfnfusmdnqgvurihza.supabase.co/rest/v1/energy_logs";
const String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImFrdmZuZnVzbWRucWd2dXJpaHphIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzM4NDAwMDEsImV4cCI6MjA4OTQxNjAwMX0.xUOCxPNaYXGrb0WdDTgDT5gpIZs0cs43Cw1UqqpFAC8";

// --- Hardware Mapping ---
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
// Hardware Serial 2 on ESP32-S3
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);

WebServer server(80);

// --- GLOBAL DATA CACHE (The "Source of Truth") ---
float c_voltage = NAN;
float c_current = NAN;
float c_power   = NAN;
float c_energy  = NAN;

// --- Timing & Aggregation ---
unsigned long lastReadTime = 0;
unsigned long lastPostTime = 0;
const unsigned long READ_INTERVAL = 2000;  // Read every 2 seconds
const unsigned long POST_INTERVAL = 60000; // Cloud upload every 60 seconds

float runningPowerSum = 0;
int readingCount = 0;
String macID = "";

// --- Dashboard HTML ---
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='2'>"; 
  html += "<title>WattWise Live</title>";
  html += "<style>body { font-family: 'Segoe UI', sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 20px; }";
  html += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 15px; max-width: 500px; margin: auto; }";
  html += ".card { background: #1e293b; padding: 20px; border-radius: 12px; border: 1px solid #334155; }";
  html += "h1 { color: #22d3ee; margin-bottom: 30px; }";
  html += ".val { font-size: 1.5rem; font-weight: bold; color: #38bdf8; } .unit { font-size: 0.9rem; color: #94a3b8; }</style></head><body>";
  
  html += "<h1>WattWise Telemetry</h1>";
  
  // Display from CACHE variables
  if (isnan(c_voltage)) {
    html += "<h2 style='color: #ef4444;'>[ERROR] Waiting for Sensor...</h2>";
  } else {
    html += "<div class='grid'>";
    html += "<div class='card'><div class='unit'>Voltage</div><div class='val'>" + String(c_voltage, 1) + " V</div></div>";
    html += "<div class='card'><div class='unit'>Current</div><div class='val'>" + String(c_current, 2) + " A</div></div>";
    html += "<div class='card'><div class='unit'>Power</div><div class='val'>" + String(c_power, 1) + " W</div></div>";
    html += "<div class='card'><div class='unit'>Energy</div><div class='val'>" + String(c_energy, 3) + " kWh</div></div>";
    html += "</div>";
  }
  
  html += "<p style='margin-top:20px; color:#64748b;'>Device ID: " + macID + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  
  // WiFiManager: Auto-connect or enter Captive Portal
  WiFiManager wm;
  Serial.println("\nStarting WiFiManager...");
  if(!wm.autoConnect("WattWise-Setup")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }

  Serial.println("Connected! Dashboard at: http://" + WiFi.localIP().toString());
  macID = WiFi.macAddress();

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  unsigned long now = millis();
  server.handleClient();

  // --- TASK 1: Read Sensor every 2 seconds ---
  if (now - lastReadTime >= READ_INTERVAL) {
    lastReadTime = now;
    
    float v = pzem.voltage();
    float p = pzem.power();
    
    if (!isnan(v)) {
      c_voltage = v;
      c_current = pzem.current();
      c_power   = p;
      c_energy  = pzem.energy();

      // For Cloud Averaging
      runningPowerSum += c_power;
      readingCount++;
    } else {
      Serial.println("PZEM Read Error");
    }
  }

  // --- TASK 2: Post to Supabase every 60 seconds ---
  if (now - lastPostTime >= POST_INTERVAL) {
    lastPostTime = now;

    if (readingCount > 0 && WiFi.status() == WL_CONNECTED) {
      float avgWatts = runningPowerSum / readingCount;

      HTTPClient http;
      http.begin(SUPABASE_URL);
      http.addHeader("apikey", SUPABASE_KEY);
      http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
      http.addHeader("Content-Type", "application/json");

      JsonDocument doc;
      doc["device_id"] = macID;
      doc["average_watts"] = avgWatts;
      doc["energy_kwh"] = c_energy;

      String json;
      serializeJson(doc, json);

      int httpResponseCode = http.POST(json);
      
      if (httpResponseCode > 0) {
        Serial.printf("[Cloud] POST Success: %d\n", httpResponseCode);
      } else {
        Serial.printf("[Cloud] POST Error: %s\n", http.errorToString(httpResponseCode).c_str());
      }
      http.end();

      // Reset aggregators
      runningPowerSum = 0;
      readingCount = 0;
    }
  }
}