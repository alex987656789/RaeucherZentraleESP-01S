#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Ticker.h>


void messen();

// DS18B20 Pin
#define ONE_WIRE_BUS 2 // GPIO2 
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// AP Credentials
const char *ssid = "ESP8266_Temp_Sensor";
const char *password = "12345678";
ESP8266WebServer server(80);

//Ticker

//Ticker timer1(messen, 500); // messen alle 1000ms

void handleRoot() {
  int count=0;
  int index=10;
  sensors.requestTemperatures();
  float tempG = sensors.getTempCByIndex(0);
  float tempU = sensors.getTempCByIndex(1);

//  String html = "<h1>R&auml;ucherofen Zentrale </h1><p>Garraum: " + String(tempG) + " &deg;C</p><p>Aussentemperatur: " + String(tempU) + " &deg;C</p>";
//  server.send(200, "text/html", html);

  String table = "<h1>R&auml;ucherofen Zentrale </h1><p>Garraum: " + String(tempG) + " &deg;C</p><p>Aussentemperatur: " + String(tempU) + " &deg;C</p><table border=\"1\"><tr><th>Index</th><th colspan=\"2\">Garraum</th><th colspan=\"2\">Umgebung</th></tr>";

  for(count=index;count >= 0; count--)
  {
      table += "<tr><td>" + String(count) + "</td><td>" + String(tempG,0) + "</td><td>&deg;C</td><td>" + String(tempU,0) + "</td><td>&deg;C</td></tr>";
      Serial.println(count);
  }
  table += "</table>";
  server.send(200, "text/html", table);
  index++;
}

void setup() {
  Serial.begin(115200);
 // timer1.start();
  WiFi.softAP(ssid, password);
  server.on("/", handleRoot);
  server.begin();
  sensors.begin();
}

void loop() {

  server.handleClient();
 // timer1.update();

 // delay(50000);
}

void messen() {

  Serial.println("Timer+");
  }
