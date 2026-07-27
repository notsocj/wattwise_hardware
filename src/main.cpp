#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>
#include <WebServer.h>

// Prototype Supabase REST configuration. Keep RLS enabled; never replace this
// public anon key with a service-role key in firmware.
const String SUPABASE_URL_LOGS = "https://akvfnfusmdnqgvurihza.supabase.co/rest/v1/energy_logs";
const String SUPABASE_URL_DEVICES = "https://akvfnfusmdnqgvurihza.supabase.co/rest/v1/devices";
const String SUPABASE_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImFrdmZuZnVzbWRucWd2dXJpaHphIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzM4NDAwMDEsImV4cCI6MjA4OTQxNjAwMX0.xUOCxPNaYXGrb0WdDTgDT5gpIZs0cs43Cw1UqqpFAC8";

#define PZEM_RX_PIN 4
#define PZEM_TX_PIN 5
#define RELAY_PIN 15
#define RESET_BTN_PIN 0 // BOOT button; the physical RST/EN button only reboots.

struct PzemPinPair {
  int rx;
  int tx;
};

const PzemPinPair PZEM_PIN_CANDIDATES[] = {
  {4, 5}, // Matches the latest committed hardware configuration.
};
const size_t PZEM_PIN_CANDIDATE_COUNT = sizeof(PZEM_PIN_CANDIDATES) / sizeof(PZEM_PIN_CANDIDATES[0]);

PZEM004Tv30 pzem(Serial2, PZEM_RX_PIN, PZEM_TX_PIN);
WebServer server(80);
WiFiManager wm;

float c_voltage = NAN;
float c_current = NAN;
float c_power = NAN;
float c_energy = NAN;
float runningPowerSum = 0;
int readingCount = 0;
String macID;
bool isRelayOn = false;

unsigned long lastReadTime = 0;
unsigned long lastCloudSyncTime = 0;
unsigned long btnPressTime = 0;
unsigned long nextRetryTime = 0;
bool isBtnPressed = false;
bool cloudConfigured = false;
bool pzemPinsLocked = false;
size_t pzemPinCandidate = 0;
uint8_t consecutivePzemFailures = 0;
uint8_t retryExponent = 0;

const unsigned long READ_INTERVAL = 2000;
const unsigned long CLOUD_SYNC_INTERVAL = 5000;
const unsigned long HTTP_TIMEOUT_MS = 8000;
const unsigned long RESET_HOLD_MS = 5000;
const unsigned long MAX_RETRY_MS = 120000;

bool isHttpSuccess(int code) {
  return code >= 200 && code < 300;
}

String normalizedMac(String value) {
  value.trim();
  value.toUpperCase();
  return value;
}

String urlEncode(const String &value) {
  String encoded;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == ':') {
      encoded += c;
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

void recordCloudFailure(const String &context, int code) {
  retryExponent = min<uint8_t>(retryExponent + 1, 5);
  unsigned long delayMs = 5000UL << retryExponent;
  nextRetryTime = millis() + min(delayMs, MAX_RETRY_MS);
  Serial.println("[CLOUD] " + context + " failed (" + String(code) + "), retry scheduled");
}

void recordCloudSuccess() {
  retryExponent = 0;
  nextRetryTime = 0;
}

void addHeaders(HTTPClient &http, bool json = false) {
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", "Bearer " + SUPABASE_KEY);
  if (json) {
    http.addHeader("Content-Type", "application/json");
  }
  http.setTimeout(HTTP_TIMEOUT_MS);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta http-equiv='refresh' content='2'><title>WattWise Live</title>";
  html += "<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;text-align:center;padding:20px}.grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;max-width:500px;margin:auto}.card{background:#1e293b;padding:20px;border-radius:12px}h1{color:#22d3ee}</style></head><body>";
  html += "<h1>WattWise Telemetry</h1><p>Relay Status: <strong style='color:";
  html += isRelayOn ? "#10b981'>ON" : "#ef4444'>OFF";
  html += "</strong></p>";
  if (isnan(c_voltage)) {
    html += "<h2 style='color:#ef4444'>Waiting for Sensor...</h2>";
  } else {
    html += "<div class='grid'><div class='card'>Voltage<br><b>" + String(c_voltage, 1) + " V</b></div>";
    html += "<div class='card'>Current<br><b>" + String(c_current, 2) + " A</b></div>";
    html += "<div class='card'>Power<br><b>" + String(c_power, 1) + " W</b></div>";
    html += "<div class='card'>Energy<br><b>" + String(c_energy, 3) + " kWh</b></div></div>";
  }
  html += "<p style='color:#64748b'>MAC Address (QR Code ID): " + macID + "</p></body></html>";
  server.send(200, "text/html", html);
}

bool verifyCloudRegistration() {
  HTTPClient http;
  String url = SUPABASE_URL_DEVICES + "?select=relay_state&mac_address=eq." + urlEncode(macID);
  if (!http.begin(url)) {
    return false;
  }
  addHeaders(http);
  const int code = http.GET();
  if (code != 200) {
    Serial.println("[SETUP] Device is not registered or Supabase is unavailable (" + String(code) + ")");
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getString());
  const bool registered = !error && doc.is<JsonArray>() && doc.as<JsonArray>().size() > 0;
  Serial.println(registered ? "[SETUP] MAC registration verified" : "[SETUP] MAC is not registered in WattWise");
  http.end();
  return registered;
}

bool connectWiFi() {
  wm.setConfigPortalTimeout(180);
  wm.setConnectTimeout(30);
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    const unsigned long holdStarted = millis();
    Serial.println("[SETUP] BOOT held during startup; keep holding for 5 seconds to clear Wi-Fi");
    while (digitalRead(RESET_BTN_PIN) == LOW && millis() - holdStarted < RESET_HOLD_MS) {
      delay(25);
    }
    if (millis() - holdStarted >= RESET_HOLD_MS) {
      Serial.println("[SETUP] Clearing Wi-Fi credentials before provisioning");
      wm.resetSettings();
      delay(250);
    }
  }
  Serial.println("[SETUP] Starting WiFiManager. Use BOOT to clear credentials; RST/EN only reboots.");
  if (!wm.autoConnect("WattWise-Setup")) {
    Serial.println("[SETUP] Wi-Fi provisioning timed out; restarting in recovery mode");
    return false;
  }
  macID = normalizedMac(WiFi.macAddress());
  Serial.println("[SETUP] Connected. MAC: " + macID);
  return true;
}

void handleButton(unsigned long now) {
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!isBtnPressed) {
      isBtnPressed = true;
      btnPressTime = now;
      Serial.println("[SETUP] BOOT held; release before 5 seconds to cancel credential reset");
    } else if (now - btnPressTime >= RESET_HOLD_MS) {
      Serial.println("[SETUP] Clearing Wi-Fi credentials and restarting");
      wm.resetSettings();
      delay(250);
      ESP.restart();
    }
  } else {
    isBtnPressed = false;
  }
}

void selectPzemPinCandidate(size_t candidate) {
  pzemPinCandidate = candidate % PZEM_PIN_CANDIDATE_COUNT;
  const PzemPinPair pins = PZEM_PIN_CANDIDATES[pzemPinCandidate];
  Serial2.end();
  delay(25);
  Serial2.begin(9600, SERIAL_8N1, pins.rx, pins.tx);
  Serial.println("[SENSOR] Probing PZEM UART: ESP32 RX GPIO" + String(pins.rx) +
                 ", TX GPIO" + String(pins.tx));
}

void readSensor(unsigned long now) {
  if (now - lastReadTime < READ_INTERVAL) {
    return;
  }
  lastReadTime = now;
  const float voltage = pzem.voltage();
  if (isnan(voltage)) {
    consecutivePzemFailures++;
    Serial.println("[SENSOR] PZEM read failed on RX GPIO" +
                   String(PZEM_PIN_CANDIDATES[pzemPinCandidate].rx) +
                   "/TX GPIO" + String(PZEM_PIN_CANDIDATES[pzemPinCandidate].tx));
    if (!pzemPinsLocked && consecutivePzemFailures >= 3) {
      consecutivePzemFailures = 0;
      selectPzemPinCandidate(pzemPinCandidate + 1);
    }
    return;
  }
  if (!pzemPinsLocked) {
    pzemPinsLocked = true;
    Serial.println("[SENSOR] PZEM detected; UART mapping locked");
  }
  consecutivePzemFailures = 0;
  c_voltage = voltage;
  c_current = pzem.current();
  c_power = pzem.power();
  c_energy = pzem.energy();
  if (!isnan(c_power) && !isnan(c_energy)) {
    runningPowerSum += c_power;
    readingCount++;
  }
}

void pollRelayState() {
  HTTPClient http;
  String url = SUPABASE_URL_DEVICES + "?select=relay_state&mac_address=eq." + urlEncode(macID);
  if (!http.begin(url)) {
    recordCloudFailure("relay GET", -1);
    return;
  }
  addHeaders(http);
  const int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, http.getString());
    if (!error && doc.is<JsonArray>() && doc.as<JsonArray>().size() > 0 && doc[0]["relay_state"].is<bool>()) {
      isRelayOn = doc[0]["relay_state"].as<bool>();
      digitalWrite(RELAY_PIN, isRelayOn ? HIGH : LOW);
      Serial.println(String("[RELAY] Command applied: ") + (isRelayOn ? "ON" : "OFF"));
    } else {
      Serial.println("[RELAY] Invalid or empty relay response; keeping fail-safe state");
      recordCloudFailure("relay JSON", code);
    }
  } else {
    recordCloudFailure("relay GET", code);
  }
  http.end();
}

void postTelemetry() {
  if (readingCount <= 0 || isnan(c_energy)) {
    return;
  }

  JsonDocument doc;
  doc["device_id"] = macID;
  doc["average_watts"] = runningPowerSum / readingCount;
  doc["energy_kwh"] = c_energy;
  doc["voltage_v"] = c_voltage;
  doc["current_a"] = c_current;
  String json;
  serializeJson(doc, json);

  HTTPClient http;
  if (!http.begin(SUPABASE_URL_LOGS)) {
    recordCloudFailure("telemetry POST", -1);
    return;
  }
  addHeaders(http, true);
  const int code = http.POST(json);
  Serial.println("[TELEMETRY] POST status: " + String(code));
  if (isHttpSuccess(code)) {
    runningPowerSum = 0;
    readingCount = 0;
    recordCloudSuccess();
  } else {
    // Keep the aggregation intact so transient failures are retried.
    recordCloudFailure("telemetry POST", code);
  }
  http.end();
}

void syncCloud(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED || now < nextRetryTime || now - lastCloudSyncTime < CLOUD_SYNC_INTERVAL) {
    return;
  }
  lastCloudSyncTime = now;
  // Relay commands are only read after MAC registration is verified. Keep
  // telemetry delivery independent so a transient registration/RLS response
  // cannot silently stop the energy buffer from being submitted.
  if (cloudConfigured) {
    pollRelayState();
  }
  postTelemetry();
}

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("\n[BOOT] WattWise ESP32-S3 starting; physical RST/EN is reboot only");
  pzem.setAddress(0x01);
  selectPzemPinCandidate(0);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // fail-safe until Supabase returns a valid command
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);

  if (!connectWiFi()) {
    delay(1000);
    ESP.restart();
  }

  cloudConfigured = verifyCloudRegistration();
  if (!cloudConfigured) {
    Serial.println("[SETUP] Register this MAC in wattwise_platform before telemetry can be accepted");
  }

  server.on("/", handleRoot);
  server.begin();
  Serial.println("[BOOT] Local telemetry page ready at http://" + WiFi.localIP().toString());
}

void loop() {
  const unsigned long now = millis();
  server.handleClient();
  handleButton(now);
  readSensor(now);
  if (!cloudConfigured && WiFi.status() == WL_CONNECTED && now >= nextRetryTime) {
    cloudConfigured = verifyCloudRegistration();
    if (!cloudConfigured) {
      recordCloudFailure("device registration", 404);
    }
  }
  syncCloud(now);
}
