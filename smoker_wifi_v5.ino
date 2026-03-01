// ═══════════════════════════════════════════════════════════════════════
// RäucherZentrale v5 — ESP-01S + DS18B20
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
#define SAMPLE_INTERVAL 60         // seconds between readings
#define MAX_READINGS    3000       // ring buffer size 
#define MAX_LINES       60         // Max lines in web table (RAM optimization)
#define CSV_CHUNK_SIZE  20         // Send CSV in chunks to avoid RAM overflow

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

// ── Web: Main Page (RAM-optimized with MAX_LINES limit) ────────────────
void handleRoot() {
    // Show latest values at top (from the last stored reading)
    float currentG = -127.0, currentU = -127.0;
    if (count > 0) {
        size_t lastIdx = (writeIndex == 0) ? MAX_READINGS - 1 : writeIndex - 1;
        currentG = readings[lastIdx].garraum;
        currentU = readings[lastIdx].umgebung;
    }

    // Send HTML in smaller parts to save RAM
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    
    // Send header
    server.sendContent(F("<!DOCTYPE html><html><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"10\">"
        "<title>R&auml;ucherzentrale</title>"
        "<style>"
        "body { font-family: sans-serif; margin: 1em; background: #f5f0eb; }"
        "h1 { color: #8B4513; }"
        ".current { font-size: 1.3em; margin: 0.5em 0; }"
        ".temp { font-weight: bold; }"
        "table { border-collapse: collapse; width: 100%; margin-top: 1em; }"
        "th { background: #8B4513; color: white; padding: 0.5em; }"
        "td { border: 1px solid #ccc; padding: 0.3em 0.6em; text-align: right; }"
        "tr:nth-child(even) { background: #ede4db; }"
        ".info { color: #666; font-size: 0.85em; margin-top: 0.5em; }"
        ".warn { color: #c00; }"
        "a.btn { display: inline-block; margin: 0.5em 0.5em 0.5em 0; padding: 0.4em 1em;"
        "background: #8B4513; color: white; text-decoration: none; border-radius: 4px; font-size: 0.9em; }"
        "a.btn:hover { background: #a0522d; }"
        "</style>"
        "</head><body>"
        "<h1>&#128293; R&auml;ucherofen Zentrale</h1>"));

    // Current temperatures
    String temp = "<p class='current'>Garraum: <span class='temp";
    if (currentG > 120) temp += " warn";
    temp += "'>" + String(currentG, 1) + " &deg;C</span></p>";
    temp += "<p class='current'>Umgebung: <span class='temp'>"
          + String(currentU, 1) + " &deg;C</span></p>";
    server.sendContent(temp);

    // Info line
    temp = "<p class='info'>Messwerte: " + String(count) + " / " + String(MAX_READINGS)
         + " &mdash; Intervall: " + String(SAMPLE_INTERVAL) + "s"
         + " &mdash; Laufzeit: " + formatTime(millis());
    
    // Add info if table is limited
    if (count > MAX_LINES) {
        temp += " &mdash; <b>Zeige letzte " + String(MAX_LINES) + " von " + String(count) + "</b>";
    }
    temp += "</p>";
    server.sendContent(temp);

    // Buttons
    server.sendContent(F("<p><a class='btn' href='/export.csv'>&#128190; CSV Export (alle Daten)</a>"
                        "<a class='btn' href='/reset'>&#128260; Reset</a></p>"));

    // Table header
    server.sendContent(F("<table><tr><th>#</th><th>Zeit</th><th>Garraum</th><th>Umgebung</th></tr>\n"));

    // Table rows - LIMITED to MAX_LINES for RAM optimization
    size_t linesToShow = min(count, (size_t)MAX_LINES);
    
    for (size_t i = 0; i < linesToShow; i++) {
        size_t idx = (writeIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        
        temp = "<tr><td>" + String(i + 1)
             + "</td><td>" + formatTime(readings[idx].timestamp)
             + "</td><td>" + String(readings[idx].garraum, 1) + " &deg;C"
             + "</td><td>" + String(readings[idx].umgebung, 1) + " &deg;C"
             + "</td></tr>\n";
        server.sendContent(temp);
        
        // Let system breathe every few rows
        if (i % 10 == 9) {
            yield();
        }
    }

    server.sendContent(F("</table></body></html>"));
    server.sendContent(""); // End chunked transfer
}

// ── Web: JSON endpoint (also limited for RAM) ──────────────────────────
void handleJson() {
    size_t n = min(count, (size_t)MAX_LINES * 2); // Limit JSON to 2x table size
    
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "[");
    
    for (size_t i = 0; i < n; i++) {
        size_t idx = (writeIndex - 1 - i + MAX_READINGS) % MAX_READINGS;
        if (i > 0) server.sendContent(",");
        
        String item = "{\"t\":" + String(readings[idx].timestamp)
                    + ",\"g\":" + String(readings[idx].garraum, 1)
                    + ",\"u\":" + String(readings[idx].umgebung, 1) + "}";
        server.sendContent(item);
        
        if (i % 20 == 19) yield();
    }
    
    server.sendContent("]");
    server.sendContent(""); // End chunked transfer
}

// ── Web: CSV export (RAM-optimized chunked sending) ────────────────────
void handleCsv() {
    size_t n = min(count, (size_t)MAX_READINGS);
    
    // Send CSV header
    server.sendHeader("Content-Disposition", "attachment; filename=\"raeucherzentrale.csv\"");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/csv", "Nr;Zeit_s;Garraum_C;Umgebung_C\n");
    
    // Send data in chunks (oldest first for chronological order)
    size_t start = (count < MAX_READINGS) ? 0 : writeIndex;
    String chunk = "";
    
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % MAX_READINGS;
        
        chunk += String(i + 1) + ";"
               + String(readings[idx].timestamp / 1000) + ";"
               + String(readings[idx].garraum, 1) + ";"
               + String(readings[idx].umgebung, 1) + "\n";
        
        // Send chunk every CSV_CHUNK_SIZE rows
        if ((i + 1) % CSV_CHUNK_SIZE == 0 || i == n - 1) {
            server.sendContent(chunk);
            chunk = "";  // Clear buffer
            yield();     // Let system process other tasks
        }
    }
    
    server.sendContent(""); // End chunked transfer
}

// ── Web: Reset buffer ───────────────────────────────────────────────────
void handleReset() {
    writeIndex = 0;
    count = 0;
    
    // Send simple HTML response with auto-redirect
    server.send(200, "text/html", 
        F("<!DOCTYPE html><html><head>"
          "<meta http-equiv='refresh' content='2;url=/'>"
          "</head><body>"
          "<h2>Buffer zur&uuml;ckgesetzt!</h2>"
          "<p>Weiterleitung in 2 Sekunden...</p>"
          "</body></html>"));
}

// ── Setup ───────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println(F("\n=== Raeucherzentrale v4 (RAM-optimized) ==="));
    Serial.printf("Freier Heap: %d bytes\n", ESP.getFreeHeap());

    sensors.begin();
    Serial.printf("Sensoren gefunden: %d\n", sensors.getDeviceCount());

    WiFi.softAP(ssid, password);
    Serial.print(F("AP IP: "));
    Serial.println(WiFi.softAPIP());

    server.on("/",          handleRoot);
    server.on("/data.json", handleJson);
    server.on("/export.csv", handleCsv);
    server.on("/reset",     handleReset);
    server.begin();

    // Take first reading immediately
    messen();

    // Start periodic sampling via Ticker
    sampler.attach(SAMPLE_INTERVAL, onSampleTick);
    
    Serial.printf("Setup abgeschlossen. Freier Heap: %d bytes\n", ESP.getFreeHeap());
}

// ── Loop ────────────────────────────────────────────────────────────────
void loop() {
    server.handleClient();

    if (sampleFlag) {
        sampleFlag = false;
        messen();
    }
}
