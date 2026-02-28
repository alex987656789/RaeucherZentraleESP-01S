// ═══════════════════════════════════════════════════════════════════════
// RäucherZentrale v3 — ESP-01S + DS18B20
// AP mode, Ticker-based sampling, ring buffer history, web UI
// ═══════════════════════════════════════════════════════════════════════
//
// Wiring: DS18B20 data → GPIO2 (with 4.7kΩ pull-up to 3.3V)
//         Multiple sensors on same bus supported.
//
// Libraries (install via Library Manager):
//   - OneWire
//   - DallasTemperature

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Ticker.h>

// ── Config ──────────────────────────────────────────────────────────────
#define ONE_WIRE_BUS    2          // GPIO2 on ESP-01S
#define SAMPLE_INTERVAL 10         // seconds between readings
#define MAX_READINGS    200        // ring buffer size (~2.4kB RAM)

const char* ssid     = "Raeucherzentrale";
const char* password = "12345678";

// ── Globals ─────────────────────────────────────────────────────────────
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
ESP8266WebServer server(80);
Ticker sampler;

struct Reading {
    unsigned long timestamp;  // millis()
    float garraum;
    float umgebung;
};

Reading readings[MAX_READINGS];
size_t writeIndex = 0;
size_t count      = 0;

// Flag — Ticker callback runs in ISR context, so only set a flag
volatile bool sampleFlag = false;

void onSampleTick() {
    sampleFlag = true;
}

// ── Take a reading (called from loop, not ISR) ─────────────────────────
void messen() {
    sensors.requestTemperatures();  // ~750ms blocking, but that's fine in loop()

    Reading r;
    r.timestamp = millis();
    r.garraum   = sensors.getTempCByIndex(0);
    r.umgebung  = sensors.getTempCByIndex(1);

    readings[writeIndex] = r;
    writeIndex = (writeIndex + 1) % MAX_READINGS;
    if (count < MAX_READINGS) count++;

    Serial.printf("[%lus] Garraum: %.1f°C  Umgebung: %.1f°C\n",
                  r.timestamp / 1000, r.garraum, r.umgebung);
}

// ── Format timestamp as mm:ss or hh:mm:ss ───────────────────────────────
String formatTime(unsigned long ms) {
    unsigned long totalSec = ms / 1000;
    unsigned long h = totalSec / 3600;
    unsigned long m = (totalSec % 3600) / 60;
    unsigned long s = totalSec % 60;

    char buf[12];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%lu:%02lu", m, s);
    }
    return String(buf);
}

// ── Web: Main Page ──────────────────────────────────────────────────────
void handleRoot() {
    // Show latest values at top (from the last stored reading)
    float currentG = -127.0, currentU = -127.0;
    if (count > 0) {
        size_t lastIdx = (writeIndex == 0) ? MAX_READINGS - 1 : writeIndex - 1;
        currentG = readings[lastIdx].garraum;
        currentU = readings[lastIdx].umgebung;
    }

    String html = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="10">
<title>R&auml;ucherzentrale</title>
<style>
  body { font-family: sans-serif; margin: 1em; background: #f5f0eb; }
  h1 { color: #8B4513; }
  .current { font-size: 1.3em; margin: 0.5em 0; }
  .temp { font-weight: bold; }
  table { border-collapse: collapse; width: 100%; margin-top: 1em; }
  th { background: #8B4513; color: white; padding: 0.5em; }
  td { border: 1px solid #ccc; padding: 0.3em 0.6em; text-align: right; }
  tr:nth-child(even) { background: #ede4db; }
  .info { color: #666; font-size: 0.85em; margin-top: 0.5em; }
  .warn { color: #c00; }
</style>
</head><body>
<h1>&#128293; R&auml;ucherofen Zentrale</h1>
)rawliteral";

    // Current temperatures
    html += "<p class='current'>Garraum: <span class='temp";
    if (currentG > 120) html += " warn";
    html += "'>" + String(currentG, 1) + " &deg;C</span></p>";
    html += "<p class='current'>Umgebung: <span class='temp'>"
          + String(currentU, 1) + " &deg;C</span></p>";

    html += "<p class='info'>Messwerte: " + String(count) + " / " + String(MAX_READINGS)
          + " &mdash; Intervall: " + String(SAMPLE_INTERVAL) + "s"
          + " &mdash; Laufzeit: " + formatTime(millis()) + "</p>";

    // History table (newest first)
    html += "<table><tr><th>#</th><th>Zeit</th><th>Garraum</th><th>Umgebung</th></tr>\n";

    size_t n = min(count, (size_t)MAX_READINGS);
    for (size_t i = 0; i < n; i++) {
        // Walk backwards from newest
        size_t idx = (writeIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        html += "<tr><td>" + String(i + 1)
              + "</td><td>" + formatTime(readings[idx].timestamp)
              + "</td><td>" + String(readings[idx].garraum, 1) + " &deg;C"
              + "</td><td>" + String(readings[idx].umgebung, 1) + " &deg;C"
              + "</td></tr>\n";
    }
    html += "</table></body></html>";

    server.send(200, "text/html", html);
}

// ── Web: JSON endpoint ──────────────────────────────────────────────────
void handleJson() {
    String json = "[";
    size_t n = min(count, (size_t)MAX_READINGS);
    for (size_t i = 0; i < n; i++) {
        size_t idx = (writeIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        if (i > 0) json += ",";
        json += "{\"t\":" + String(readings[idx].timestamp)
              + ",\"g\":" + String(readings[idx].garraum, 1)
              + ",\"u\":" + String(readings[idx].umgebung, 1) + "}";
    }
    json += "]";
    server.send(200, "application/json", json);
}

// ── Web: Reset buffer ───────────────────────────────────────────────────
void handleReset() {
    writeIndex = 0;
    count = 0;
    server.send(200, "text/plain", "Buffer geleert.");
}

// ── Setup ───────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Raeucherzentrale v3 ===");

    sensors.begin();
    Serial.printf("Sensoren gefunden: %d\n", sensors.getDeviceCount());

    WiFi.softAP(ssid, password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    server.on("/",          handleRoot);
    server.on("/data.json", handleJson);
    server.on("/reset",     handleReset);
    server.begin();

    // Take first reading immediately
    messen();

    // Start periodic sampling via Ticker
    sampler.attach(SAMPLE_INTERVAL, onSampleTick);
}

// ── Loop ────────────────────────────────────────────────────────────────
void loop() {
    server.handleClient();

    if (sampleFlag) {
        sampleFlag = false;
        messen();
    }
}
