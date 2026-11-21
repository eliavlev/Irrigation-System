/*
#####################################################################################
#   File:        Watering_System.ino
#   Processor:   NodeMCU v3 (ESP8266 / ESP-12E)
#   Objectives:  Irrigation controller for 3 valves, controlled over MQTT
#   Author:      Eliav Levinson
#   Date:        30/12/16 (original Blynk version), MQTT version 2026
#####################################################################################

  This project contains public domain code.
  The modification is allowed without notice.

  The controller is self-sufficient: schedules, rain mode and manual duration are
  stored in flash, and the clock comes from NTP, so watering keeps running when the
  MQTT broker or the internet is down. MQTT is used to change settings and report state.

  MQTT topics (prefix irrigation/<DEVICE_ID>):
    set/valve/<1-3>/schedule  retained  {"start":360,"stop":380,"days":[0,1,0,1,0,1,0]}
                                         start/stop = minutes since midnight (-1 = off),
                                         days = Sun..Sat
    set/valve/<1-3>/manual              "1" start a manual run, "0" stop it
    set/rain                  retained  "1" rain mode on (schedules paused), "0" off
    set/manual_minutes        retained  manual run duration in minutes
    state                     retained  JSON snapshot published by the device
    status                    retained  "online" / "offline" (last will)
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FS.h>
#include "secrets.h"     // Credentials (git-ignored). Copy secrets.h.example to create it.
#include "ca_certs.h"

#ifndef SECRET_MQTT_HOST
#error "secrets.h is missing the MQTT settings - compare it with secrets.h.example"
#endif

//------------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------------
#define ON  0                       // relay modules are active LOW
#define OFF 1
#define NUM_VALVES 3
#define UNSET_TIME 2500             // never matches a real time: valve slot is off

#define WIFI_CONNECT_TIMEOUT_MS   20000UL
#define WIFI_FORCE_RECONNECT_MS  300000UL
#define MQTT_RETRY_MS             15000UL
#define WATERING_CHECK_MS          5000UL
#define MOISTURE_CHECK_MS         30000UL
#define STATE_PUBLISH_MS          60000UL
#define DEFAULT_MANUAL_MINUTES    10

// Local time zone (POSIX TZ string). Default: Israel (IST/IDT).
// Examples: "CET-1CEST,M3.5.0,M10.5.0/3" (Central Europe), "EST5EDT,M3.2.0,M11.1.0" (US Eastern).
#define LOCAL_TZ    "IST-2IDT,M3.4.4/26,M10.5.0"
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"

const int moistureSensorPin = A0;

//------------------------------------------------------------------------------------
// State
//------------------------------------------------------------------------------------
struct Valve {
  uint8_t pin;
  int start;                        // minutes since midnight, UNSET_TIME = off
  int stop;
  uint8_t days[8];                  // [0]=valve id, [1..7]=Sun..Sat (same layout as old flash files)
  bool manual;
  unsigned long manualStart;
  bool on;
};

Valve valves[NUM_VALVES] = {
  {12, UNSET_TIME, UNSET_TIME, {1,0,0,0,0,0,0,0}, false, 0, false},   // Valve1: D6
  {14, UNSET_TIME, UNSET_TIME, {2,0,0,0,0,0,0,0}, false, 0, false},   // Valve2: D5
  { 2, UNSET_TIME, UNSET_TIME, {3,0,0,0,0,0,0,0}, false, 0, false},   // Valve3: D4
};

bool rainMode = false;
int manualMinutes = DEFAULT_MANUAL_MINUTES;
int moistureValue = 0;
bool spiffsActive = false;
bool oledOk = false;
bool stateDirty = true;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

BearSSL::WiFiClientSecure net;
BearSSL::X509List trustAnchors;
PubSubClient mqtt(net);

String topicBase;                   // irrigation/<DEVICE_ID>

//------------------------------------------------------------------------------------
// Flash storage (file names kept from the Blynk version so saved schedules survive)
//------------------------------------------------------------------------------------
int ReadIntFromFlash(const String& filename, int fallback){
  if (!spiffsActive || !SPIFFS.exists(filename)) return fallback;
  File f = SPIFFS.open(filename, "r");
  if (!f) return fallback;
  String s;
  while (f.position() < f.size()) {          // last line wins (matches old format)
    s = f.readStringUntil('\n');
    s.trim();
  }
  f.close();
  return s.length() ? s.toInt() : fallback;
}

void WriteIntToFlash(const String& filename, int value){
  if (!spiffsActive) return;
  File f = SPIFFS.open(filename, "w");
  if (!f) { Serial.println("Unable to write " + filename); return; }
  f.println(value);
  f.close();
}

bool ReadDaysFromFlash(const String& filename, uint8_t days[8]){
  if (!spiffsActive || !SPIFFS.exists(filename)) return false;
  File f = SPIFFS.open(filename, "r");
  if (!f) return false;
  f.read(days, 8);
  f.close();
  return true;
}

void WriteDaysToFlash(const String& filename, const uint8_t days[8]){
  if (!spiffsActive) return;
  File f = SPIFFS.open(filename, "w");
  if (!f) { Serial.println("Unable to write " + filename); return; }
  f.write(days, 8);
  f.close();
}

// Load a schedule time (minutes since midnight). Falls back to the pre-v11 file,
// which stored HHMM with midnight as 24xx, and migrates it.
int LoadScheduleTime(const String& newName, const String& oldName){
  int v = ReadIntFromFlash(newName, UNSET_TIME);
  if (v >= 0 && v < 1440) return v;

  int old = ReadIntFromFlash(oldName, -1);
  int hh = old / 100, mm = old % 100;
  if (hh == 24) hh = 0;
  if (old >= 0 && hh < 24 && mm < 60) {
    v = hh * 60 + mm;
    Serial.println("Migrated " + oldName + " -> " + newName);
    WriteIntToFlash(newName, v);
    return v;
  }
  return UNSET_TIME;
}

void LoadSettings(){
  for (int i = 0; i < NUM_VALVES; i++) {
    String n = String(i + 1);
    valves[i].start = LoadScheduleTime("StartMin_v" + n, "Start_v" + n);
    valves[i].stop  = LoadScheduleTime("StopMin_v" + n,  "Stop_v" + n);
    ReadDaysFromFlash("WeekV" + n, valves[i].days);
    valves[i].days[0] = i + 1;
    Serial.printf("Valve%d: start=%d stop=%d days=", i + 1, valves[i].start, valves[i].stop);
    for (int d = 1; d <= 7; d++) Serial.print(valves[i].days[d]);
    Serial.println();
  }
  rainMode = ReadIntFromFlash("RainMode", 0) == 1;
  manualMinutes = ReadIntFromFlash("ManualMin", DEFAULT_MANUAL_MINUTES);
  if (manualMinutes <= 0) manualMinutes = DEFAULT_MANUAL_MINUTES;
}

//------------------------------------------------------------------------------------
// Clock (NTP via the ESP8266 SDK, local time via TZ rules)
//------------------------------------------------------------------------------------
bool ClockValid(){
  return time(nullptr) > 1609459200;          // after 2021-01-01: SNTP has answered
}

bool LocalNow(struct tm& lt){
  if (!ClockValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &lt);
  return true;
}

// True if the current time is inside the [start, stop] window (minutes since midnight,
// stop inclusive) on a selected day. A window with start > stop crosses midnight
// (e.g. 23:00-01:30); its after-midnight part belongs to the day it started on.
bool InWindow(const Valve& v, const struct tm& lt){
  if (v.start < 0 || v.start >= 1440 || v.stop < 0 || v.stop >= 1440) return false;
  int nowMin = lt.tm_hour * 60 + lt.tm_min;
  int today = lt.tm_wday + 1;                   // 1=Sun .. 7=Sat
  if (v.start <= v.stop) {
    return v.days[today] == 1 && nowMin >= v.start && nowMin <= v.stop;
  }
  if (nowMin >= v.start) return v.days[today] == 1;
  if (nowMin <= v.stop) {
    int yesterday = (today == 1) ? 7 : today - 1;
    return v.days[yesterday] == 1;
  }
  return false;
}

//------------------------------------------------------------------------------------
// Display
//------------------------------------------------------------------------------------
void UpdateDisplay(){
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  struct tm lt;
  if (LocalNow(lt)) {
    display.printf("%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
  } else {
    display.print("Clock not set");
  }
  for (int i = 0; i < NUM_VALVES; i++) {
    display.setCursor(0, 20 + i * 10);
    display.printf("Valve%d %s", i + 1, valves[i].on ? (valves[i].manual ? "Manual" : "Watering") : "STBY");
  }
  display.setCursor(0, 54);
  display.printf("%s %s%s", WiFi.status() == WL_CONNECTED ? "WiFi" : "NoWiFi",
                 mqtt.connected() ? "MQTT" : "NoMQTT", rainMode ? " RAIN" : "");
  display.display();
}

//------------------------------------------------------------------------------------
// Watering logic
//------------------------------------------------------------------------------------
void WateringCheck(){
  struct tm lt;
  bool clockOk = LocalNow(lt);
  unsigned long manualMs = (unsigned long)manualMinutes * 60UL * 1000UL;

  for (int i = 0; i < NUM_VALVES; i++) {
    Valve& v = valves[i];
    if (v.manual && millis() - v.manualStart >= manualMs) {
      v.manual = false;
      Serial.printf("Valve%d manual run finished\n", i + 1);
      stateDirty = true;
    }
    bool scheduled = clockOk && !rainMode && InWindow(v, lt);
    bool shouldBeOn = scheduled || v.manual;
    digitalWrite(v.pin, shouldBeOn ? ON : OFF);
    if (shouldBeOn != v.on) {
      v.on = shouldBeOn;
      Serial.printf("Valve%d %s\n", i + 1, v.on ? "ON" : "OFF");
      stateDirty = true;
    }
  }
  UpdateDisplay();
}

void CheckMoisture(){
  // Capacitive sensor: dry ~728, wet ~452
  int value = analogRead(moistureSensorPin);
  if (abs(value - moistureValue) > 5) stateDirty = true;
  moistureValue = value;
}

//------------------------------------------------------------------------------------
// MQTT
//------------------------------------------------------------------------------------
void PublishState(){
  if (!mqtt.connected()) return;
  JsonDocument doc;
  struct tm lt;
  if (LocalNow(lt)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    doc["time"] = buf;
  } else {
    doc["time"] = nullptr;
  }
  doc["rain"] = rainMode ? 1 : 0;
  doc["manual_minutes"] = manualMinutes;
  doc["moisture"] = moistureValue;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  unsigned long manualMs = (unsigned long)manualMinutes * 60UL * 1000UL;
  JsonArray arr = doc["valves"].to<JsonArray>();
  for (int i = 0; i < NUM_VALVES; i++) {
    const Valve& v = valves[i];
    JsonObject o = arr.add<JsonObject>();
    o["on"] = v.on;
    o["manual"] = v.manual;
    o["manual_left"] = v.manual ? (long)((manualMs - (millis() - v.manualStart)) / 1000) : 0;
    o["start"] = (v.start < 1440) ? v.start : -1;
    o["stop"]  = (v.stop  < 1440) ? v.stop  : -1;
    JsonArray days = o["days"].to<JsonArray>();
    for (int d = 1; d <= 7; d++) days.add(v.days[d]);
  }
  char payload[700];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  mqtt.publish((topicBase + "/state").c_str(), (const uint8_t*)payload, n, true);
  stateDirty = false;
}

void ApplySchedule(int idx, const char* json){
  JsonDocument doc;
  if (deserializeJson(doc, json)) { Serial.println("Bad schedule JSON"); return; }
  Valve& v = valves[idx];
  int start = doc["start"] | -1;
  int stop  = doc["stop"]  | -1;
  v.start = (start >= 0 && start < 1440) ? start : UNSET_TIME;
  v.stop  = (stop  >= 0 && stop  < 1440) ? stop  : UNSET_TIME;
  JsonArray days = doc["days"];
  for (int d = 0; d < 7; d++) v.days[d + 1] = (days[d] | 0) ? 1 : 0;

  String n = String(idx + 1);
  WriteIntToFlash("StartMin_v" + n, v.start);
  WriteIntToFlash("StopMin_v" + n, v.stop);
  WriteDaysToFlash("WeekV" + n, v.days);
  Serial.printf("Valve%d schedule: %d-%d\n", idx + 1, v.start, v.stop);
}

void OnMqttMessage(char* topic, byte* payload, unsigned int length){
  String t(topic);
  String p;
  p.reserve(length);
  for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
  Serial.println("MQTT " + t + " = " + p);

  String prefix = topicBase + "/set/";
  if (!t.startsWith(prefix)) return;
  String cmd = t.substring(prefix.length());

  if (cmd.startsWith("valve/")) {
    int idx = cmd.substring(6, 7).toInt() - 1;          // "valve/<n>/..."
    if (idx < 0 || idx >= NUM_VALVES) return;
    String what = cmd.substring(8);
    if (what == "schedule") {
      ApplySchedule(idx, p.c_str());
    } else if (what == "manual") {
      bool start = (p == "1");
      if (start) valves[idx].manualStart = millis();      // a new press restarts the timer
      valves[idx].manual = start;
    }
  } else if (cmd == "rain") {
    rainMode = (p == "1");
    WriteIntToFlash("RainMode", rainMode ? 1 : 0);
  } else if (cmd == "manual_minutes") {
    int m = p.toInt();
    if (m > 0 && m <= 240) {
      manualMinutes = m;
      WriteIntToFlash("ManualMin", m);
    }
  }
  stateDirty = true;
  WateringCheck();                   // act on the change right away
}

void MqttConnect(){
  static unsigned long lastAttempt = 0;
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (!ClockValid()) return;          // TLS certificate checks need the correct time
  if (lastAttempt && millis() - lastAttempt < MQTT_RETRY_MS) return;
  lastAttempt = millis();

  Serial.printf("Connecting to MQTT %s:%d ... ", SECRET_MQTT_HOST, SECRET_MQTT_PORT);
  String clientId = String("irrigation-") + SECRET_DEVICE_ID + "-" + String(ESP.getChipId(), HEX);
  String statusTopic = topicBase + "/status";
  if (mqtt.connect(clientId.c_str(), SECRET_MQTT_USER, SECRET_MQTT_PASS,
                   statusTopic.c_str(), 1, true, "offline")) {
    Serial.println("connected");
    mqtt.publish(statusTopic.c_str(), "online", true);
    mqtt.subscribe((topicBase + "/set/#").c_str(), 1);
    stateDirty = true;
  } else {
    char err[80] = "";
    net.getLastSSLError(err, sizeof(err));
    Serial.printf("failed, rc=%d %s\n", mqtt.state(), err);
  }
}

//------------------------------------------------------------------------------------
// WiFi
//------------------------------------------------------------------------------------
void CheckWifiStatus(){
  static bool wasConnected = true;
  static unsigned long downSince = 0;
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected) {
    if (!wasConnected) Serial.println("WiFi reconnected, IP: " + WiFi.localIP().toString());
  } else if (wasConnected) {
    Serial.println("WiFi connection lost");
    downSince = millis();
  } else if (millis() - downSince >= WIFI_FORCE_RECONNECT_MS) {
    Serial.println("WiFi still down - forcing reconnect");
    WiFi.reconnect();
    downSince = millis();
  }
  wasConnected = connected;
}

//------------------------------------------------------------------------------------
// Setup / loop
//------------------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);
  Serial.println();

  for (int i = 0; i < NUM_VALVES; i++) {
    pinMode(valves[i].pin, OUTPUT);
    digitalWrite(valves[i].pin, OFF);
  }

  spiffsActive = SPIFFS.begin();
  Serial.println(spiffsActive ? "SPIFFS Active" : "Unable to activate SPIFFS");
  LoadSettings();

  Wire.begin();
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false);
  if (oledOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.print("Connecting WiFi...");
    display.display();
  }

  Serial.print("Connecting to ");
  Serial.println(SECRET_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi not available - continuing offline, will keep retrying");
  }

  configTime(LOCAL_TZ, NTP_SERVER1, NTP_SERVER2);

  topicBase = String("irrigation/") + SECRET_DEVICE_ID;
  trustAnchors.append(ISRG_ROOT_X1);
  trustAnchors.append(ISRG_ROOT_X2);
  net.setTrustAnchors(&trustAnchors);
  net.setTimeout(10000);
  mqtt.setServer(SECRET_MQTT_HOST, SECRET_MQTT_PORT);
  mqtt.setCallback(OnMqttMessage);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(10);

  CheckMoisture();
  WateringCheck();
}

void loop() {
  static unsigned long lastWatering = 0, lastMoisture = 0, lastWifi = 0, lastPublish = 0;
  static bool clockReported = false;
  unsigned long now = millis();

  if (!clockReported && ClockValid()) {
    clockReported = true;
    Serial.println("Clock set from NTP");
  }

  MqttConnect();
  mqtt.loop();

  if (now - lastWatering >= WATERING_CHECK_MS) { lastWatering = now; WateringCheck(); }
  if (now - lastMoisture >= MOISTURE_CHECK_MS) { lastMoisture = now; CheckMoisture(); }
  if (now - lastWifi >= 10000UL)               { lastWifi = now; CheckWifiStatus(); }

  if (mqtt.connected() && (stateDirty || now - lastPublish >= STATE_PUBLISH_MS)) {
    static unsigned long lastDirtyPublish = 0;
    if (!stateDirty || now - lastDirtyPublish >= 1000UL) {   // at most 1 change-update per second
      lastDirtyPublish = now;
      lastPublish = now;
      PublishState();
    }
  }
}
