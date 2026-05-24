#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <WebServer.h>

// --- Supabase Configuration ---
const String SUPABASE_URL_LOGS = "https://akvfnfusmdnqgvurihza.supabase.co/rest/v1/energy_logs";
const String SUPABASE_URL_DEVICES = "https://akvfnfusmdnqgvurihza.supabase.co/rest/v1/devices";
const String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImFrdmZuZnVzbWRucWd2dXJpaHphIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzM4NDAwMDEsImV4cCI6MjA4OTQxNjAwMX0.xUOCxPNaYXGrb0WdDTgDT5gpIZs0cs43Cw1UqqpFAC8";

// --- Hardware Mapping ---
#define PZEM_RX_PIN 4   // Moved away from USB Pin 18 -> Connect to PZEM TX
#define PZEM_TX_PIN 5   // Moved away from USB Pin 19 -> Connect to PZEM RX
#define RELAY_PIN 15    // Controls your relay (Pin 15)
#define RESET_BTN_PIN 0 // The built-in "BOOT" button on the ESP32-S3

// Hardware Serial 2 on ESP32-S3
PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
WebServer server(80);

// --- GLOBAL DATA CACHE ---
float c_voltage = NAN;
float c_current = NAN;
float c_power   = NAN;
float c_energy  = NAN;

// --- Timing & Aggregation ---
unsigned long lastReadTime = 0;
unsigned long lastCloudSyncTime = 0;
const unsigned long READ_INTERVAL = 2000;       // Sensor read every 2s
const unsigned long CLOUD_SYNC_INTERVAL = 5000; // POST data & GET relay state every 5s

float runningPowerSum = 0;
int readingCount = 0;
String macID = "";
bool isRelayOn = false; 

// For Factory Reset Logic
unsigned long btnPressTime = 0;
bool isBtnPressed = false;

// --- Helper: Setup WiFiManager ---
WiFiManager wm;

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='2'>"; 
  html += "<title>WattWise Live</title>";
  html += "<style>body { font-family: sans-serif; background: #0f172a; color: #f8fafc; text-align: center; padding: 20px; }";
  html += ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; max-width: 500px; margin: auto; }";
  html += ".card { background: #1e293b; padding: 20px; border-radius: 12px; }";
  html += "h1 { color: #22d3ee; }</style></head><body>";
  
  html += "<h1>WattWise Telemetry</h1>";
  html += "<p>Relay Status: <strong style='color:" + String(isRelayOn ? "#10b981" : "#ef4444") + "'>" + String(isRelayOn ? "ON" : "OFF") + "</strong></p>";
  
  if (isnan(c_voltage)) {
    html += "<h2 style='color: #ef4444;'>Waiting for Sensor...</h2>";
  } else {
    html += "<div class='grid'>";
    html += "<div class='card'>Voltage<br><b>" + String(c_voltage, 1) + " V</b></div>";
    html += "<div class='card'>Current<br><b>" + String(c_current, 2) + " A</b></div>";
    html += "<div class='card'>Power<br><b>" + String(c_power, 1) + " W</b></div>";
    html += "<div class='card'>Energy<br><b>" + String(c_energy, 3) + " kWh</b></div>";
    html += "</div>";
  }
  
  html += "<p style='color:#64748b;'>MAC Address (QR Code ID): " + macID + "</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // LOW = OFF when hardware jumper is set to High-Trigger "H"
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  Serial.println("\nStarting WiFiManager...");
  if(!wm.autoConnect("WattWise-Setup")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }

  macID = WiFi.macAddress();
  Serial.println("Connected! MAC ID: " + macID);

  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  unsigned long now = millis();
  server.handleClient();

  // --- HARDWARE RESET LOGIC ---
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!isBtnPressed) {
      isBtnPressed = true;
      btnPressTime = now;
    } else if (now - btnPressTime > 5000) {
      Serial.println("\n[FACTORY RESET] Wiping WiFi Credentials...");
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    isBtnPressed = false;
  }

  // --- SENSOR READ LOGIC ---
  if (now - lastReadTime >= READ_INTERVAL) {
    lastReadTime = now;
    float v = pzem.voltage();
    if (!isnan(v)) {
      c_voltage = v;
      c_current = pzem.current();
      c_power   = pzem.power();
      c_energy  = pzem.energy();
      runningPowerSum += c_power;
      readingCount++;
    }
  }

  // --- CLOUD SYNC LOGIC (Telemetry & Relay Control) ---
  if (now - lastCloudSyncTime >= CLOUD_SYNC_INTERVAL) {
    lastCloudSyncTime = now;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      
      // 1. FETCH RELAY STATE FROM SUPABASE
      String urlGet = SUPABASE_URL_DEVICES + "?select=relay_state&mac_address=eq." + macID;
      http.begin(urlGet);
      http.addHeader("apikey", SUPABASE_KEY);
      http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
      
      int getCode = http.GET();
      Serial.println("\n[HTTP GET] Relay Check Code: " + String(getCode));
      
      if (getCode == 200) {
        String payload = http.getString();
        Serial.println("[HTTP GET] Payload: " + payload);
        
        // High-Trigger (H Jumper Configured)
        if (payload.indexOf("\"relay_state\":true") > 0) {
          isRelayOn = true;
          digitalWrite(RELAY_PIN, HIGH); // Send 3.3V to turn ON
          Serial.println("--> Relay commanded ON");
        } else if (payload.indexOf("\"relay_state\":false") > 0) {
          isRelayOn = false;
          digitalWrite(RELAY_PIN, LOW);  // Send 0V to turn OFF
          Serial.println("--> Relay commanded OFF");
        }
      } else {
        Serial.println("[HTTP GET] Failed. Error: " + http.getString());
      }
      http.end();

      // 2. POST TELEMETRY TO SUPABASE
      if (readingCount > 0) {
        float avgWatts = runningPowerSum / readingCount;
        
        http.begin(SUPABASE_URL_LOGS);
        http.addHeader("apikey", SUPABASE_KEY);
        http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["device_id"] = macID;
        doc["average_watts"] = avgWatts;
        doc["energy_kwh"] = c_energy;
        doc["voltage_v"] = c_voltage;
        doc["current_a"] = c_current;

        String json;
        serializeJson(doc, json);
        int postCode = http.POST(json);
        
        Serial.println("[HTTP POST] Telemetry Code: " + String(postCode));
        if (postCode > 0) {
          runningPowerSum = 0;
          readingCount = 0;
        } else {
           Serial.println("[HTTP POST] Failed. Error: " + http.errorToString(postCode));
        }
        http.end();
      } else {
        Serial.println("[SENSOR] No PZEM data to send yet. Voltage: " + String(c_voltage));
      }
    }
  }
}