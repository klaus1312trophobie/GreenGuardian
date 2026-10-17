// =============================================================================
//  Green Guardian – Cannabis grow monitor for ESP32
//  DHT22 (temp/humidity) + capacitive soil moisture sensor + 0.96" SSD1306 OLED
//  Web dashboard, Telegram bot, 10 s rolling averages, min/max, 24 h history
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_system.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <time.h>

#include "config.h"
#include "webpage.h"

// ----------------------------------------------------------------------------
//  Hardware / services
// ----------------------------------------------------------------------------
DHT dht(PIN_DHT, DHT22);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WebServer server(80);
const char *resetReasonText();
WiFiClientSecure tgClient;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, tgClient);
Preferences prefs;

bool oledOk = false;
bool apActive = false;
bool bootMsgSent = false;

// ----------------------------------------------------------------------------
//  Rolling 10 s average (1 sample per second)
// ----------------------------------------------------------------------------
constexpr int AVG_N = 10;

struct Ring {
  float v[AVG_N];
  int idx = 0;
  Ring() { for (auto &x : v) x = NAN; }
  void push(float x) { v[idx] = x; idx = (idx + 1) % AVG_N; }
  float avg() const {
    float s = 0; int n = 0;
    for (float x : v) if (!isnan(x)) { s += x; n++; }
    return n ? s / n : NAN;
  }
};

struct MinMax {
  float lo = NAN, hi = NAN;
  void add(float x) {
    if (isnan(x)) return;
    if (isnan(lo) || x < lo) lo = x;
    if (isnan(hi) || x > hi) hi = x;
  }
  void reset() { lo = hi = NAN; }
};

Ring tempRing, humRing, rawRing;
MinMax mmTemp, mmHum, mmSoil;
time_t mmSince = 0;

int samples = 0;
uint32_t dhtErrors = 0;
float avgTemp = NAN, avgHum = NAN, avgRaw = NAN;
int soilPct = -1;

// ----------------------------------------------------------------------------
//  Settings (persisted)
// ----------------------------------------------------------------------------
int dryRaw = DEFAULT_DRY_RAW;
int wetRaw = DEFAULT_WET_RAW;
int alertThreshold = DEFAULT_ALERT_THRESHOLD;
int reportHour = DEFAULT_REPORT_HOUR;   // -1 = daily report off
time_t lastWatered = 0;
int lastReportKey = 0;                  // year*1000 + yday of last daily report

// ----------------------------------------------------------------------------
//  24 h history: one point every 5 minutes
// ----------------------------------------------------------------------------
constexpr uint32_t HEAP_MIN_FREE = 25000;   // restart below this many free bytes
constexpr uint32_t HEAP_MIN_BLOCK = 12000;  // ...or if the largest free block shrinks below this
constexpr int HIST_N = 288;
constexpr uint32_t HIST_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr int16_t NO_VAL = INT16_MIN;

struct HistPoint { uint32_t t; int16_t soil; int16_t temp10; int16_t hum10; };
HistPoint hist[HIST_N];
int histHead = 0, histCount = 0;

// ----------------------------------------------------------------------------
//  Alert state
// ----------------------------------------------------------------------------
bool dryAlertActive = false;
uint32_t dryAlertLastMs = 0;
constexpr uint32_t DRY_REMINDER_MS = 6UL * 3600UL * 1000UL;  // remind every 6 h
bool hotAlertActive = false, coldAlertActive = false;

// Watering detection: soil % every 30 s over the last 3 minutes
int waterWindow[6];
int waterWindowIdx = 0, waterWindowCount = 0;

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
bool timeValid() { return time(nullptr) > 1700000000; }

String fmt(float v, int dec, const char *unit = "") {
  if (isnan(v)) return "--";
  return String(v, dec) + unit;
}

String timeAgo(time_t t) {
  if (t == 0 || !timeValid()) return "never";
  long s = time(nullptr) - t;
  if (s < 0) s = 0;
  if (s < 3600) return String(s / 60) + " min ago";
  if (s < 86400) return String(s / 3600) + " h ago";
  return String(s / 86400) + " d " + String((s % 86400) / 3600) + " h ago";
}

int rawToPct(float raw) {
  if (isnan(raw)) return -1;
  float span = (float)dryRaw - (float)wetRaw;
  if (fabsf(span) < 1) return -1;
  float p = ((float)dryRaw - raw) * 100.0f / span;
  return constrain((int)lroundf(p), 0, 100);
}

float svp(float t) { return 0.6108f * expf(17.27f * t / (t + 237.3f)); }

// Leaf VPD in kPa
float calcVpd(float t, float rh) {
  if (isnan(t) || isnan(rh)) return NAN;
  float v = svp(t - LEAF_TEMP_OFFSET) - svp(t) * rh / 100.0f;
  return v < 0 ? 0 : v;
}

float calcDewPoint(float t, float rh) {
  if (isnan(t) || isnan(rh) || rh <= 0) return NAN;
  const float a = 17.27f, b = 237.7f;
  float g = a * t / (b + t) + logf(rh / 100.0f);
  return b * g / (a - g);
}

float calcHeatIndex(float t, float rh) {
  if (isnan(t) || isnan(rh)) return NAN;
  return dht.computeHeatIndex(t, rh, false);
}

const char *vpdStage(float v) {
  if (isnan(v)) return "--";
  if (v < 0.4f) return "too low - mold risk";
  if (v < 0.8f) return "seedlings / clones";
  if (v < 1.2f) return "vegetative sweet spot";
  if (v < 1.6f) return "flowering sweet spot";
  return "too high - plants stressed";
}

// ----------------------------------------------------------------------------
//  Persistence
// ----------------------------------------------------------------------------
void loadSettings() {
  prefs.begin("guardian", true);
  dryRaw = prefs.getInt("dry", DEFAULT_DRY_RAW);
  wetRaw = prefs.getInt("wet", DEFAULT_WET_RAW);
  alertThreshold = prefs.getInt("thresh", DEFAULT_ALERT_THRESHOLD);
  reportHour = prefs.getInt("repHour", DEFAULT_REPORT_HOUR);
  lastWatered = prefs.getULong("watered", 0);
  lastReportKey = prefs.getInt("repKey", 0);
  mmTemp.lo = prefs.getFloat("tLo", NAN); mmTemp.hi = prefs.getFloat("tHi", NAN);
  mmHum.lo  = prefs.getFloat("hLo", NAN); mmHum.hi  = prefs.getFloat("hHi", NAN);
  mmSoil.lo = prefs.getFloat("sLo", NAN); mmSoil.hi = prefs.getFloat("sHi", NAN);
  mmSince = prefs.getULong("mmSince", 0);
  prefs.end();
}

void saveSettings() {
  prefs.begin("guardian", false);
  prefs.putInt("dry", dryRaw);
  prefs.putInt("wet", wetRaw);
  prefs.putInt("thresh", alertThreshold);
  prefs.putInt("repHour", reportHour);
  prefs.putULong("watered", lastWatered);
  prefs.putInt("repKey", lastReportKey);
  prefs.end();
}

void saveMinMax() {
  prefs.begin("guardian", false);
  prefs.putFloat("tLo", mmTemp.lo); prefs.putFloat("tHi", mmTemp.hi);
  prefs.putFloat("hLo", mmHum.lo);  prefs.putFloat("hHi", mmHum.hi);
  prefs.putFloat("sLo", mmSoil.lo); prefs.putFloat("sHi", mmSoil.hi);
  prefs.putULong("mmSince", mmSince);
  prefs.end();
}

// ----------------------------------------------------------------------------
//  Telegram
// ----------------------------------------------------------------------------
bool tgEnabled() { return strlen(TELEGRAM_BOT_TOKEN) > 10 && strlen(TELEGRAM_CHAT_ID) > 0; }

// Single-shot send: bot.sendMessage() re-POSTs for up to 8 s when the reply is slow
// (weak WiFi), which delivered the same message 2-3 times.
bool tgSendTo(const String &chatId, const String &msg, const char *parseMode = "HTML") {
  JsonDocument doc;
  JsonObject payload = doc.to<JsonObject>();
  payload["chat_id"] = chatId;
  payload["text"] = msg;
  if (strlen(parseMode)) payload["parse_mode"] = parseMode;
  String response = bot.sendPostToTelegram(bot.buildCommand("sendMessage"), payload);
  tgClient.stop();
  return bot.checkForOkResponse(response);
}

// Returns the real result (Telegram confirmed delivery); used by the dashboard test button.
bool tgSendChecked(const String &msg) {
  if (!tgEnabled() || WiFi.status() != WL_CONNECTED) return false;
  return tgSendTo(TELEGRAM_CHAT_ID, msg);
}

// Fire-and-forget for alerts/reports: never retry, a lost reply usually still means delivered.
bool tgSend(const String &msg) {
  if (!tgEnabled() || WiFi.status() != WL_CONNECTED) return false;
  tgSendTo(TELEGRAM_CHAT_ID, msg);
  return true;
}

String statusReport(const char *title) {
  float vpd = calcVpd(avgTemp, avgHum);
  String s = "🌿 <b>" + String(title) + "</b>\n\n";
  s += "💧 Soil moisture: <b>" + (soilPct < 0 ? String("--") : String(soilPct) + " %") + "</b>\n";
  s += "🌡️ Temperature: <b>" + fmt(avgTemp, 1, " °C") + "</b>\n";
  s += "💨 Air humidity: <b>" + fmt(avgHum, 1, " %") + "</b>\n";
  s += "🍃 VPD: <b>" + fmt(vpd, 2, " kPa") + "</b> (" + vpdStage(vpd) + ")\n";
  s += "❄️ Dew point: " + fmt(calcDewPoint(avgTemp, avgHum), 1, " °C") + "\n\n";
  s += "📈 <b>Min / Max</b>\n";
  s += "  Soil: " + fmt(mmSoil.lo, 0, "%") + " / " + fmt(mmSoil.hi, 0, "%") + "\n";
  s += "  Temp: " + fmt(mmTemp.lo, 1, "°C") + " / " + fmt(mmTemp.hi, 1, "°C") + "\n";
  s += "  Hum:  " + fmt(mmHum.lo, 1, "%") + " / " + fmt(mmHum.hi, 1, "%") + "\n\n";
  s += "🚿 Last watered: " + timeAgo(lastWatered) + "\n";
  if (soilPct >= 0 && soilPct < alertThreshold) s += "\n⚠️ <b>Your girls are thirsty – time to water!</b>";
  else s += "\n✅ Plants are happy. Stay green! 🌱";
  return s;
}

void markWatered(bool notify) {
  if (timeValid()) lastWatered = time(nullptr);
  dryAlertActive = false;
  saveSettings();
  if (notify) tgSend("🚿 Watering detected – soil now at <b>" + String(soilPct) + " %</b>. Thanks for taking care of the girls! 💚");
}

void pollTelegram() {
  if (!tgEnabled() || WiFi.status() != WL_CONNECTED) return;
  int n = bot.getUpdates(bot.last_message_received + 1);
  while (n) {
    for (int i = 0; i < n; i++) {
      telegramMessage &m = bot.messages[i];
      if (m.chat_id != TELEGRAM_CHAT_ID) {
        tgSendTo(m.chat_id, "🚫 This is a private grow. Your chat ID is: " + m.chat_id, "");
        continue;
      }
      String cmd = m.text;
      cmd.trim();
      if (cmd.startsWith("/status")) {
        tgSend(statusReport("Grow Status"));
      } else if (cmd.startsWith("/watered")) {
        markWatered(false);
        tgSend("🚿 Watering logged. 💚");
      } else {
        String help = "🌿 <b>Green Guardian</b>\n\n"
                      "/status – current values\n"
                      "/watered – log a watering\n"
                      "/help – this message\n\n"
                      "Dashboard: http://" + WiFi.localIP().toString() + "  or  http://" HOSTNAME ".local";
        tgSend(help);
      }
    }
    n = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ----------------------------------------------------------------------------
//  Sensors
// ----------------------------------------------------------------------------
int readSoilRaw() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) { sum += analogRead(PIN_SOIL); delayMicroseconds(200); }
  return sum / 16;
}

void sampleSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) { dhtErrors++; t = h = NAN; }
  tempRing.push(t);
  humRing.push(h);
  rawRing.push(readSoilRaw());
  if (samples < AVG_N) samples++;

  avgTemp = tempRing.avg();
  avgHum = humRing.avg();
  avgRaw = rawRing.avg();
  soilPct = rawToPct(avgRaw);

  // only track min/max once the 10 s window is full
  if (samples >= AVG_N) {
    if (mmSince == 0 && timeValid()) mmSince = time(nullptr);
    mmTemp.add(avgTemp);
    mmHum.add(avgHum);
    if (soilPct >= 0) mmSoil.add(soilPct);
  }
}

void recordHistory() {
  HistPoint &p = hist[histHead];
  p.t = timeValid() ? (uint32_t)time(nullptr) : 0;
  p.soil = soilPct >= 0 ? soilPct : NO_VAL;
  p.temp10 = isnan(avgTemp) ? NO_VAL : (int16_t)lroundf(avgTemp * 10);
  p.hum10 = isnan(avgHum) ? NO_VAL : (int16_t)lroundf(avgHum * 10);
  histHead = (histHead + 1) % HIST_N;
  if (histCount < HIST_N) histCount++;
}

void checkWatering() {
  if (soilPct < 0) return;
  waterWindow[waterWindowIdx] = soilPct;
  waterWindowIdx = (waterWindowIdx + 1) % 6;
  if (waterWindowCount < 6) waterWindowCount++;
  int lowest = 101;
  for (int i = 0; i < waterWindowCount; i++) lowest = min(lowest, waterWindow[i]);
  bool recentlyLogged = timeValid() && lastWatered && time(nullptr) - lastWatered < 1800;
  if (soilPct - lowest >= 15 && !recentlyLogged) {
    markWatered(true);
    waterWindowCount = 0;
  }
}

void checkAlerts() {
  if (samples < AVG_N) return;

  // --- soil moisture ---
  if (soilPct >= 0) {
    if (soilPct < alertThreshold) {
      if (!dryAlertActive || millis() - dryAlertLastMs > DRY_REMINDER_MS) {
        bool reminder = dryAlertActive;
        if (tgSend(String(reminder ? "⏰ Reminder: " : "") +
                   "🥵 <b>Water me!</b>\nSoil moisture dropped to <b>" + String(soilPct) +
                   " %</b> (threshold " + String(alertThreshold) + " %).\nYour girls are thirsty 🌿💧")) {
          dryAlertActive = true;
          dryAlertLastMs = millis();
        }
      }
    } else if (soilPct >= alertThreshold + 5) {
      dryAlertActive = false;
    }
  }

  // --- temperature ---
  if (!isnan(avgTemp)) {
    if (!hotAlertActive && avgTemp > TEMP_HIGH_ALERT) {
      if (tgSend("🔥 <b>Too hot!</b> Temperature is " + fmt(avgTemp, 1, " °C") + ". Check ventilation.")) hotAlertActive = true;
    } else if (hotAlertActive && avgTemp < TEMP_HIGH_ALERT - 1) hotAlertActive = false;

    if (!coldAlertActive && avgTemp < TEMP_LOW_ALERT) {
      if (tgSend("🥶 <b>Too cold!</b> Temperature is " + fmt(avgTemp, 1, " °C") + ".")) coldAlertActive = true;
    } else if (coldAlertActive && avgTemp > TEMP_LOW_ALERT + 1) coldAlertActive = false;
  }
}

void checkDailyReport() {
  if (reportHour < 0 || !timeValid()) return;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  int key = (lt.tm_year + 1900) * 1000 + lt.tm_yday;
  if (lt.tm_hour == reportHour && key != lastReportKey) {
    if (tgSend(statusReport("Daily Grow Report"))) {
      lastReportKey = key;
      saveSettings();
    }
  }
}

// ----------------------------------------------------------------------------
//  OLED
// ----------------------------------------------------------------------------
void centerText(const String &s, int y, int size) {
  int16_t x1, y1; uint16_t w, h;
  oled.setTextSize(size);
  oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  oled.setCursor((128 - w) / 2, y);
  oled.print(s);
}

void header(const char *title) {
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(title);
  oled.setCursor(104, 0);
  oled.print(WiFi.status() == WL_CONNECTED ? "WiFi" : (apActive ? " AP " : " -- "));
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);
}

void footerMinMax(float lo, float hi, int dec, const char *unit) {
  oled.setTextSize(1);
  String s = "min " + fmt(lo, dec, unit) + "  max " + fmt(hi, dec, unit);
  centerText(s, 56, 1);
}

// One serrated leaflet, angle 0 = straight up
void drawLeaflet(int bx, int by, float angDeg, float len) {
  float a = angDeg * DEG_TO_RAD;
  float dx = sinf(a), dy = -cosf(a);
  float px = -dy, py = dx;
  float w = len * 0.17f;
  auto halfW = [&](float f) { return f <= 0.45f ? w * f / 0.45f : w * (1 - f) / 0.55f; };
  auto ex = [&](float f, int s) { return bx + dx * len * f + px * s * halfW(f); };
  auto ey = [&](float f, int s) { return by + dy * len * f + py * s * halfW(f); };

  int tx = bx + dx * len, ty = by + dy * len;
  oled.fillTriangle(bx, by, ex(0.45f, 1), ey(0.45f, 1), tx, ty, SSD1306_WHITE);
  oled.fillTriangle(bx, by, ex(0.45f, -1), ey(0.45f, -1), tx, ty, SSD1306_WHITE);
  if (len < 12) return;
  for (int s = -1; s <= 1; s += 2) {
    for (float f = 0.3f; f < 0.85f; f += 0.13f) {
      float x2 = ex(f + 0.07f, s), y2 = ey(f + 0.07f, s);
      oled.fillTriangle(ex(f, s), ey(f, s), x2, y2,
                        x2 + px * s * 2.0f + dx * 1.5f, y2 + py * s * 2.0f + dy * 1.5f, SSD1306_WHITE);
    }
  }
}

void drawLeafPage() {
  const int bx = 64, by = 44;
  const float leaflets[][2] = {{0, 30}, {-35, 26}, {35, 26}, {-70, 20}, {70, 20}, {-108, 11}, {108, 11}};
  for (auto &l : leaflets) drawLeaflet(bx, by, l[0], l[1]);
  oled.fillRect(bx - 1, by, 2, 10, SSD1306_WHITE);
  centerText("~ stay green ~", 56, 1);
}

void drawPage(int page, bool blink) {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  switch (page) {
    case 0: {
      header("SOIL MOISTURE");
      centerText(soilPct < 0 ? String("--") : String(soilPct) + "%", 16, 3);
      if (soilPct >= 0) {
        oled.drawRect(4, 42, 120, 9, SSD1306_WHITE);
        oled.fillRect(6, 44, 116 * soilPct / 100, 5, SSD1306_WHITE);
        int tx = 4 + 120 * alertThreshold / 100;
        oled.drawFastVLine(tx, 40, 13, SSD1306_WHITE);
      }
      if (soilPct >= 0 && soilPct < alertThreshold) {
        if (blink) centerText("!! WATER ME !!", 56, 1);
      } else {
        footerMinMax(mmSoil.lo, mmSoil.hi, 0, "%");
      }
      break;
    }
    case 1: {
      header("TEMPERATURE");
      oled.setTextSize(3);
      String v = fmt(avgTemp, 1);
      int16_t x1, y1; uint16_t w, h;
      oled.getTextBounds(v + "oC", 0, 0, &x1, &y1, &w, &h);
      oled.setCursor((128 - w) / 2, 20);
      oled.print(v);
      oled.setTextSize(1);
      oled.write(248);          // degree sign (cp437)
      oled.setTextSize(3);
      oled.print("C");
      footerMinMax(mmTemp.lo, mmTemp.hi, 1, "");
      break;
    }
    case 2:
      header("AIR HUMIDITY");
      centerText(fmt(avgHum, 1, "%"), 20, 3);
      footerMinMax(mmHum.lo, mmHum.hi, 0, "%");
      break;
    case 3: {
      float vpd = calcVpd(avgTemp, avgHum);
      header("VPD (LEAF)");
      centerText(fmt(vpd, 2, ""), 16, 3);
      centerText("kPa", 40, 1);
      centerText(vpdStage(vpd), 54, 1);
      break;
    }
    default:
      drawLeafPage();
      break;
  }
  oled.display();
}

// page sequence: soil, temp, hum, vpd, soil, temp, hum, vpd, leaf ...
const int PAGE_SEQ[] = {0, 1, 2, 3, 0, 1, 2, 3, 4};
int pageSeqIdx = 0;

void bootScreen(const String &line) {
  if (!oledOk) return;
  oled.clearDisplay();
  drawLeafPage();
  oled.fillRect(0, 54, 128, 10, SSD1306_BLACK);
  centerText(line, 56, 1);
  oled.display();
}

// ----------------------------------------------------------------------------
//  Web server
// ----------------------------------------------------------------------------
void putNum(JsonObject o, const char *k, float v, int dec) {
  if (isnan(v)) o[k] = nullptr;
  else o[k] = serialized(String(v, dec));
}

void handleData() {
  JsonDocument doc;
  JsonObject d = doc.to<JsonObject>();
  // keys "moisture", "temp", "humidity" are used by the Android widget
  if (soilPct >= 0) d["moisture"] = soilPct; else d["moisture"] = nullptr;
  putNum(d, "temp", avgTemp, 1);
  putNum(d, "humidity", avgHum, 1);
  putNum(d, "soilRaw", avgRaw, 0);
  float vpd = calcVpd(avgTemp, avgHum);
  putNum(d, "vpd", vpd, 2);
  d["vpdStage"] = vpdStage(vpd);
  putNum(d, "dewPoint", calcDewPoint(avgTemp, avgHum), 1);
  putNum(d, "heatIndex", calcHeatIndex(avgTemp, avgHum), 1);

  JsonObject mm = d["minmax"].to<JsonObject>();
  putNum(mm, "tempMin", mmTemp.lo, 1); putNum(mm, "tempMax", mmTemp.hi, 1);
  putNum(mm, "humMin", mmHum.lo, 1);   putNum(mm, "humMax", mmHum.hi, 1);
  putNum(mm, "soilMin", mmSoil.lo, 0); putNum(mm, "soilMax", mmSoil.hi, 0);
  mm["since"] = (uint32_t)mmSince;

  d["dryRaw"] = dryRaw;
  d["wetRaw"] = wetRaw;
  d["threshold"] = alertThreshold;
  d["reportHour"] = reportHour;
  d["lastWatered"] = (uint32_t)lastWatered;
  d["now"] = timeValid() ? (uint32_t)time(nullptr) : 0;
  d["uptime"] = millis() / 1000;
  d["freeHeap"] = ESP.getFreeHeap();
  d["minHeap"] = ESP.getMinFreeHeap();
  d["maxAlloc"] = ESP.getMaxAllocHeap();
  d["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  d["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  d["telegram"] = tgEnabled();
  d["dhtOk"] = !isnan(avgTemp);
  d["dhtErrors"] = dhtErrors;
  d["warmingUp"] = samples < AVG_N;
  d["resetReason"] = resetReasonText();

  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", out);
}

void handleHistory() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  String chunk;
  int start = (histHead - histCount + HIST_N) % HIST_N;
  for (int i = 0; i < histCount; i++) {
    const HistPoint &p = hist[(start + i) % HIST_N];
    chunk += i ? ",[" : "[";
    chunk += String(p.t) + ",";
    chunk += (p.soil == NO_VAL ? String("null") : String(p.soil)) + ",";
    chunk += (p.temp10 == NO_VAL ? String("null") : String(p.temp10 / 10.0f, 1)) + ",";
    chunk += (p.hum10 == NO_VAL ? String("null") : String(p.hum10 / 10.0f, 1)) + "]";
    if (chunk.length() > 1024) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "]";
  server.sendContent(chunk);
  server.sendContent("");
}

void sendResult(bool ok, const String &msg) {
  server.send(ok ? 200 : 400, "application/json",
              String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}");
}

void handleCalibrate() {
  String type = server.arg("type");
  if (samples < AVG_N || isnan(avgRaw)) return sendResult(false, "Sensor still warming up, try again in a few seconds.");
  int raw = lroundf(avgRaw);
  if (type == "wet") {
    if (abs(raw - dryRaw) < 200) return sendResult(false, "Reading is too close to the dry value - is the sensor in wet soil?");
    wetRaw = raw;
    saveSettings();
    soilPct = rawToPct(avgRaw);
    markWatered(false);
    sendResult(true, "Calibrated: 100% = " + String(raw));
  } else if (type == "dry") {
    if (abs(raw - wetRaw) < 200) return sendResult(false, "Reading is too close to the wet value - is the sensor dry?");
    dryRaw = raw;
    saveSettings();
    soilPct = rawToPct(avgRaw);
    sendResult(true, "Calibrated: 0% = " + String(raw));
  } else {
    sendResult(false, "Unknown calibration type");
  }
}

void handleResetMinMax() {
  mmTemp.reset(); mmHum.reset(); mmSoil.reset();
  mmSince = timeValid() ? time(nullptr) : 0;
  saveMinMax();
  sendResult(true, "Min/Max reset");
}

void handleSettings() {
  if (server.hasArg("threshold")) alertThreshold = constrain(server.arg("threshold").toInt(), 5, 95);
  if (server.hasArg("reportHour")) reportHour = constrain(server.arg("reportHour").toInt(), -1, 23);
  saveSettings();
  sendResult(true, "Settings saved");
}

void handleWatered() {
  markWatered(false);
  sendResult(true, "Watering logged");
}

void handleTelegramTest() {
  if (!tgEnabled()) return sendResult(false, "Telegram is not configured (see config.h)");
  bool ok = tgSendChecked(statusReport("Test from the dashboard"));
  sendResult(ok, ok ? "Message sent" : "Sending failed");
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/data", HTTP_GET, handleData);
  server.on("/history", HTTP_GET, handleHistory);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/reset-minmax", HTTP_POST, handleResetMinMax);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/watered", HTTP_POST, handleWatered);
  server.on("/api/telegram-test", HTTP_POST, handleTelegramTest);
  server.onNotFound([] { server.send(404, "text/plain", "404 - nothing growing here"); });
  server.begin();
}

// ----------------------------------------------------------------------------
//  WiFi
// ----------------------------------------------------------------------------
// Weak signal (~-80 dBm): modem sleep adds latency and packet loss, so keep the radio awake at full power.
void wifiTune() {
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
}

// Why did the chip (re)boot? Shown in the Telegram boot message and in /data.
const char *resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout (power supply!)";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_EXT:       return "reset pin";
    default:                return "unknown";
  }
}

void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiTune();
  bootScreen("Connecting WiFi...");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected: http://%s\n", WiFi.localIP().toString().c_str());
    bootScreen(WiFi.localIP().toString());
  } else {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    apActive = true;
    Serial.printf("WiFi failed - AP '%s' at http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    bootScreen("AP " + WiFi.softAPIP().toString());
  }
  delay(2500);
}

void maintainWifi() {
  if (apActive && WiFi.status() == WL_CONNECTED) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wifiTune();
    apActive = false;
  }
  if (!bootMsgSent && WiFi.status() == WL_CONNECTED && tgEnabled()) {
    bootMsgSent = tgSend("🌱 <b>Green Guardian is online</b>\nDashboard: http://" + WiFi.localIP().toString() +
                         "\nReset reason: " + String(resetReasonText()) + "\nSend /help for commands.");
  }
}

// ----------------------------------------------------------------------------
//  Setup / loop
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Green Guardian ===");
  Serial.printf("Reset reason: %s\n", resetReasonText());

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL, ADC_11db);
  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    oled.setRotation(2);      // display mounted upside down -> rotate 180°
    oled.cp437(true);
    oled.setTextColor(SSD1306_WHITE);
  } else {
    Serial.println("OLED not found - check wiring / address");
  }
  bootScreen("Green Guardian");
  delay(1500);

  loadSettings();
  setupWifi();

  configTzTime(TZ_INFO, NTP_SERVER);
  tgClient.setInsecure();

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] { bootScreen("OTA update..."); });
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  setupWeb();
}

// Heap fragmentation guard: restart if memory gets critically low (settings live in NVS, so nothing is lost)
void checkHeap() {
  if (ESP.getFreeHeap() < HEAP_MIN_FREE || ESP.getMaxAllocHeap() < HEAP_MIN_BLOCK) {
    Serial.printf("Low heap (free %u, block %u) - restarting\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    saveMinMax();
    delay(200);
    ESP.restart();
  }
}

void loop() {
  static uint32_t tSample = 0, tPage = 0, tRedraw = 0, tHist = 0, tWater = 0,
                  tAlert = 0, tTg = 0, tPersist = 0, tDaily = 0, tHeap = 0;
  static bool histSeeded = false;
  uint32_t now = millis();

  server.handleClient();
  ArduinoOTA.handle();

  if (now - tSample >= 1000) { tSample = now; sampleSensors(); }

  if (oledOk) {
    if (now - tPage >= 3000) {
      tPage = now;
      pageSeqIdx = (pageSeqIdx + 1) % (sizeof(PAGE_SEQ) / sizeof(PAGE_SEQ[0]));
    }
    if (now - tRedraw >= 500) {
      tRedraw = now;
      drawPage(PAGE_SEQ[pageSeqIdx], (now / 500) % 2);
    }
  }

  if (samples >= AVG_N && (!histSeeded || now - tHist >= HIST_INTERVAL_MS)) {
    tHist = now; histSeeded = true; recordHistory();
  }
  if (now - tWater >= 30000) { tWater = now; checkWatering(); }
  if (now - tAlert >= 10000) { tAlert = now; maintainWifi(); checkAlerts(); }
  if (now - tDaily >= 30000) { tDaily = now; checkDailyReport(); }
  if (now - tTg >= 8000) { tTg = now; pollTelegram(); }
  if (now - tHeap >= 60000) { tHeap = now; checkHeap(); }
  if (now - tPersist >= 15UL * 60UL * 1000UL) { tPersist = now; saveMinMax(); }
}
