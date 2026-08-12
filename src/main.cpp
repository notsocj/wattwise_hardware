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

// WiFiManager serves this portal directly from the ESP32, so all branding is
// embedded and remains available while the phone has no internet connection.
const char WATTWISE_PORTAL_HEAD[] = R"rawliteral(
<style>
:root{color-scheme:dark;--ww-green:#00e66f;--ww-bg:#090b0a;--ww-surface:#151816;--ww-line:#353a37;--ww-muted:#a4aaa6}
*{box-sizing:border-box}
html{background:var(--ww-bg)}
body,.invert{margin:0!important;background:radial-gradient(circle at 50% -10%,#17211b 0,transparent 34%),var(--ww-bg)!important;color:#fff!important;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif!important;min-height:100vh}
.wrap{width:100%!important;max-width:460px!important;min-width:0!important;margin:0 auto!important;padding:24px 22px 20px!important}
.wrap>h1,.wrap>h3{display:none!important}
.ww-brand{display:flex;align-items:center;justify-content:center;gap:10px;margin:2px 0 34px}
.ww-brand img{width:42px;height:42px;display:block}
.ww-wordmark{font-size:26px;font-weight:760;letter-spacing:-1.1px;color:#f7f7f7}
.ww-wordmark b{color:var(--ww-green)}
.ww-eyebrow{margin:0 0 8px;color:var(--ww-green);font-size:13px;font-weight:800;letter-spacing:.14em}
.ww-title{margin:0;font-size:clamp(43px,12vw,64px);line-height:.98;letter-spacing:-.055em;font-weight:800;color:#fff}
.ww-step{display:flex;align-items:center;gap:12px;margin:16px 0 14px;color:var(--ww-muted);font-size:20px}
.ww-step strong{display:grid;place-items:center;width:38px;height:38px;border-radius:50%;background:var(--ww-green);color:#06130b;font-size:17px}
.ww-copy{margin:0 0 18px;color:var(--ww-muted);font-size:17px;line-height:1.42}
.ww-network{display:flex;align-items:center;min-height:52px;margin:0!important;padding:0 16px!important;background:rgba(22,24,23,.94);border:1px solid var(--ww-line);border-bottom:0!important;overflow:hidden}
.ww-network.ww-first{border-radius:14px 14px 0 0}
.ww-network.ww-last{border-bottom:1px solid var(--ww-line)!important;border-radius:0 0 14px 14px;margin-bottom:20px!important}
.ww-network.ww-selected{background:linear-gradient(90deg,rgba(0,230,111,.14),rgba(0,230,111,.04));border-color:rgba(0,230,111,.5)}
.ww-network a[data-ssid]{display:flex!important;align-items:center;flex:1;min-width:0;margin:0!important;padding:14px 0!important;color:#f6f7f6!important;font-size:17px;font-weight:600;text-decoration:none;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.ww-network.ww-selected a[data-ssid]{color:var(--ww-green)!important}
.ww-network .q{margin-left:auto!important;filter:grayscale(1) brightness(1.8)}
.ww-network.ww-selected .q{filter:hue-rotate(85deg) saturate(4) brightness(1.2)}
form{margin:0!important;padding:0!important;background:transparent!important}
form[action$='wifisave']{position:relative}
label{color:var(--ww-muted)!important;font-size:15px!important}
.ww-has-networks label[for=s],.ww-has-networks #s{display:none!important}
input[type=password],input[type=text]{width:100%!important;height:52px!important;margin:8px 0 14px!important;padding:0 92px 0 16px!important;border:1px solid #4b504d!important;border-radius:12px!important;background:#171918!important;color:#fff!important;font-size:17px!important;box-shadow:none!important}
input:focus{border-color:var(--ww-green)!important;outline:2px solid rgba(0,230,111,.15)!important}
#showpass{position:absolute!important;opacity:0!important;pointer-events:none!important}
label[for=showpass]{position:absolute;top:43px;right:16px;color:var(--ww-green)!important;font-weight:700;cursor:pointer}
button{width:100%!important;min-height:54px!important;margin:8px 0 0!important;border:0!important;border-radius:12px!important;background:var(--ww-green)!important;color:#05140b!important;font-size:17px!important;font-weight:800!important;box-shadow:none!important}
button:hover,button:focus{background:#19ef7d!important;transform:none!important}
form[action='/wifi'] button,button[name=refresh]{margin-top:8px!important;background:transparent!important;color:var(--ww-green)!important;border:1px solid transparent!important;font-weight:700!important}
.msg{border:1px solid var(--ww-line)!important;border-radius:12px!important;background:var(--ww-surface)!important;color:var(--ww-muted)!important}
.ww-footer{margin:24px 0 0!important;text-align:center;color:#737a76;font-size:13px;letter-spacing:.02em}
@media(max-width:360px){.wrap{padding-left:16px!important;padding-right:16px!important}.ww-brand{margin-bottom:28px}.ww-title{font-size:42px}.ww-copy{font-size:16px}}
</style>
<script>
document.addEventListener('DOMContentLoaded',function(){
  var links=Array.prototype.slice.call(document.querySelectorAll('a[data-ssid]'));
  links.forEach(function(link){
    var row=link.parentElement;
    row.classList.add('ww-network');
    link.addEventListener('click',function(){
      links.forEach(function(item){item.parentElement.classList.remove('ww-selected')});
      row.classList.add('ww-selected');
    });
  });
  if(links.length){
    document.body.classList.add('ww-has-networks');
    links[0].parentElement.classList.add('ww-first');
    links[links.length-1].parentElement.classList.add('ww-last');
  }
  var password=document.getElementById('p');
  if(password){password.placeholder='Enter Wi-Fi password'}
  var save=document.querySelector("form[action$='wifisave'] button[type='submit']");
  if(save){save.textContent='Connect device'}
  var refresh=document.querySelector("button[name='refresh']");
  if(refresh){refresh.textContent='Scan again'}
  var show=document.querySelector("label[for='showpass']");
  if(show){show.textContent='Show password'}
});
</script>
)rawliteral";

const char WATTWISE_PORTAL_HEADER[] = R"rawliteral(
<div class="ww-brand">
  <img alt="WattWise" src="data:image/png;base64,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" />
  <span class="ww-wordmark">Watt<b>Wise</b></span>
</div>
<section class="ww-intro">
  <p class="ww-eyebrow">DEVICE SETUP</p>
  <h2 class="ww-title">Connect your<br>WattWise</h2>
  <div class="ww-step"><strong>1</strong><span>of 2 &middot; Wi-Fi</span></div>
  <p class="ww-copy">Choose your 2.4 GHz home network.<br>Your password stays on this device.</p>
</section>
)rawliteral";

float c_voltage = NAN;
float c_current = NAN;
float c_power = NAN;
float c_energy = NAN;
float runningPowerSum = 0;
int readingCount = 0;
String macID;
String portalFooterHtml;
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
  WiFi.mode(WIFI_STA);
  delay(50);
  macID = normalizedMac(WiFi.macAddress());

  wm.setTitle("WattWise Device Setup");
  wm.setHostname("wattwise");
  wm.setMinimumSignalQuality(10);
  wm.setRemoveDuplicateAPs(true);
  wm.setCustomHeadElement(WATTWISE_PORTAL_HEAD);
  wm.setCustomBodyHeader(WATTWISE_PORTAL_HEADER);
  portalFooterHtml = "<div class='ww-footer'>Device MAC: " + macID + "</div>";
  wm.setCustomBodyFooter(portalFooterHtml.c_str());
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
