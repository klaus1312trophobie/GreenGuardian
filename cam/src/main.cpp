// =============================================================================
//  Green Guardian – tent camera (AI-Thinker ESP32-CAM, OV2640)
//  Full-resolution stills on demand, daily Telegram photo (also stored on the
//  SD card), UDP heartbeat so the sensor ESP32 can show the picture in its
//  dashboard. WiFi / Telegram / OTA settings come from ../include/config.h.
//
//  The camera never calls Telegram getUpdates: the sensor ESP32 is the only
//  poller of the shared bot (two pollers would steal each other's messages).
// =============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FS.h>
#include <SD_MMC.h>
#include <esp_camera.h>
#include <esp_system.h>
#include <time.h>

#include "config.h"
#include "cam_link.h"

// ----------------------------------------------------------------------------
//  AI-Thinker ESP32-CAM pin map
// ----------------------------------------------------------------------------
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22
#define PIN_FLASH_LED    4   // white LED – always off (light in the dark period stresses the plants)
#define PIN_STATUS_LED  33   // small red LED on the back, active low

WebServer server(80);
WiFiUDP udp;
Preferences prefs;

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------
struct Shot {
  uint8_t *buf = nullptr;   // copy of the last JPEG (PSRAM)
  size_t len = 0, cap = 0;
  uint16_t w = 0, h = 0;
  time_t t = 0;             // wall clock of the capture (0 = no time sync yet)
  uint32_t ms = 0;          // millis() of the capture
} shot;

bool camOk = false;
const char *sensorName = "none";
uint32_t captureFails = 0;
int jpegQ = CAM_JPEG_QUALITY;   // current JPEG quality number (raised automatically if a frame does not fit)

// Picture tuning: sensor settings that can be changed while the camera runs
// (/api/tune or the camera's own web page). Stored in flash, survive restarts and updates.
struct Tune {
  int brightness = 0;    // -2..2
  int contrast = 0;      // -2..2
  int saturation = 0;    // -2..2
  int ae_level = 0;      // -2..2  exposure target (darker .. brighter)
  int aec2 = 0;          // 0/1    the sensor's second auto-exposure algorithm
  int gainceiling = 0;   // 0..6   maximum analog gain 2x..128x (low = less noise, needs light)
  int sharpness = -1;    // -1 = leave the sensor default, 0..6 = manual (higher = sharper), 7 = automatic
  int quality = CAM_JPEG_QUALITY;   // 0..63, lower = better
} tune;
int settleFrames = 0;    // extra frames to drop after a settings change (exposure has to settle)

bool sdOk = false;
uint32_t sdSaved = 0;        // photos written since boot
String sdLastFile;

volatile bool tgRequested = false;   // set by /api/telegram-photo, served in loop()
int lastPhotoKey = 0;                // year*1000 + yday of the last daily photo
int photoTryKey = 0, photoTries = 0; // retry limiter for the daily photo
uint32_t photoTryMs = 0;
time_t tgLastOk = 0;
String tgLastResult = "nothing sent yet";

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------
bool timeValid() { return time(nullptr) > 1700000000; }
bool tgEnabled() { return strlen(TELEGRAM_BOT_TOKEN) > 10 && strlen(TELEGRAM_CHAT_ID) > 0; }

String timeStr(time_t t, const char *format) {
  if (t < 1700000000) return "";
  struct tm lt;
  localtime_r(&t, &lt);
  char b[32];
  strftime(b, sizeof(b), format, &lt);
  return b;
}

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

// ----------------------------------------------------------------------------
//  Camera
// ----------------------------------------------------------------------------
bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  c.fb_count = 1;
  if (psramFound()) {
    c.frame_size = FRAMESIZE_UXGA;        // 1600x1200 = full OV2640 resolution
    c.jpeg_quality = CAM_JPEG_QUALITY;
    c.fb_location = CAMERA_FB_IN_PSRAM;
  } else {                                // no PSRAM: the big frame does not fit
    c.frame_size = FRAMESIZE_SVGA;
    c.jpeg_quality = 12;
    c.fb_location = CAMERA_FB_IN_DRAM;
    Serial.println("No PSRAM found - falling back to 800x600");
  }

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  switch (s->id.PID) {
    case OV2640_PID: sensorName = "OV2640"; break;
    case OV3660_PID: sensorName = "OV3660"; break;
    case OV5640_PID: sensorName = "OV5640"; break;
    default:         sensorName = "other";  break;
  }
  s->set_vflip(s, CAM_VFLIP);
  s->set_hmirror(s, CAM_HMIRROR);
  s->set_whitebal(s, 1);      // auto white balance (grow lights have an odd spectrum)
  s->set_awb_gain(s, 1);
  s->set_exposure_ctrl(s, 1); // auto exposure
  s->set_gain_ctrl(s, 1);     // auto gain
  s->set_lenc(s, 1);          // lens shading correction (wide-angle lenses darken the corners)
  s->set_bpc(s, 1);           // black pixel correction
  s->set_wpc(s, 1);           // white pixel correction
  return true;
}

void loadTune() {
  sensor_t *s = esp_camera_sensor_get();
  prefs.begin("ggcam", true);
  tune.brightness  = prefs.getInt("tBri", 0);
  tune.contrast    = prefs.getInt("tCon", 0);
  tune.saturation  = prefs.getInt("tSat", 0);
  tune.ae_level    = prefs.getInt("tAe", 0);
  tune.aec2        = prefs.getInt("tAec2", s ? s->status.aec2 : 0);          // default: what the driver set
  tune.gainceiling = prefs.getInt("tGc", s ? s->status.gainceiling : 0);
  tune.sharpness   = prefs.getInt("tSha", -1);
  tune.quality     = prefs.getInt("tQ", CAM_JPEG_QUALITY);
  prefs.end();
}

void saveTune() {
  prefs.begin("ggcam", false);
  prefs.putInt("tBri", tune.brightness);
  prefs.putInt("tCon", tune.contrast);
  prefs.putInt("tSat", tune.saturation);
  prefs.putInt("tAe", tune.ae_level);
  prefs.putInt("tAec2", tune.aec2);
  prefs.putInt("tGc", tune.gainceiling);
  prefs.putInt("tSha", tune.sharpness);
  prefs.putInt("tQ", tune.quality);
  prefs.end();
}

void applyTune() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_brightness(s, tune.brightness);
  s->set_contrast(s, tune.contrast);
  s->set_saturation(s, tune.saturation);
  s->set_ae_level(s, tune.ae_level);
  s->set_aec2(s, tune.aec2);
  s->set_gainceiling(s, (gainceiling_t)tune.gainceiling);
  jpegQ = tune.quality;
  s->set_quality(s, jpegQ);
  // The driver has no sharpness function for the OV2640; the sensor's edge enhancement is set
  // through its DSP registers 0x92/0x93 (same sequence as the hx-esp32-cam-fpv project).
  if (tune.sharpness >= 0 && s->id.PID == OV2640_PID) {
    static const uint8_t LEVEL[7] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    s->set_reg(s, 0x92, 0xFF, 0x01);
    s->set_reg(s, 0x93, 0x20, tune.sharpness == 7 ? 0x20 : 0x00);   // bit 5: automatic / manual
    if (tune.sharpness < 7) {
      s->set_reg(s, 0x92, 0xFF, 0x01);
      s->set_reg(s, 0x93, 0x1F, LEVEL[tune.sharpness]);
    }
  }
  settleFrames = 8;
}

// Takes a fresh full-resolution picture into `shot`.
bool capture() {
  if (!camOk) return false;
  // With one frame buffer the driver hands out the frame that was grabbed right after the
  // previous capture, i.e. an old one -> drop two frames so the picture is current.
  int drop = 2 + settleFrames;
  settleFrames = 0;
  for (int i = 0; i < drop; i++) {
    camera_fb_t *old = esp_camera_fb_get();
    if (old) esp_camera_fb_return(old);
  }
  camera_fb_t *fb = nullptr;
  bool ok = false;
  for (int attempt = 0; attempt < 3 && !ok; attempt++) {
    fb = esp_camera_fb_get();
    ok = fb && fb->format == PIXFORMAT_JPEG && fb->len > 1024 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
    if (ok || attempt == 2) break;
    // No frame: a very detailed picture can be too big for the driver's buffer at this quality.
    // Compress a little more and try again; the new setting is kept until the next restart.
    if (fb) { esp_camera_fb_return(fb); fb = nullptr; }
    if (jpegQ < 16) {
      jpegQ += 2;
      sensor_t *s = esp_camera_sensor_get();
      s->set_quality(s, jpegQ);
      Serial.printf("No frame - JPEG quality number raised to %d\n", jpegQ);
    }
  }
  if (ok && fb->len > shot.cap) {
    free(shot.buf);
    shot.cap = fb->len + 64 * 1024;
    shot.buf = (uint8_t *)(psramFound() ? ps_malloc(shot.cap) : malloc(shot.cap));
    if (!shot.buf) { shot.cap = 0; shot.len = 0; ok = false; }
  }
  if (ok) {
    memcpy(shot.buf, fb->buf, fb->len);
    shot.len = fb->len;
    shot.w = fb->width;
    shot.h = fb->height;
    shot.t = timeValid() ? time(nullptr) : 0;
    shot.ms = millis();
    captureFails = 0;
  }
  if (fb) esp_camera_fb_return(fb);
  if (!ok) {
    captureFails++;
    Serial.printf("Capture failed (%u in a row)\n", captureFails);
    if (captureFails >= 5) {            // camera hangs -> only a restart brings it back
      Serial.println("Camera not responding - restarting");
      delay(200);
      ESP.restart();
    }
  }
  return ok;
}

bool shotFresh() { return shot.len && millis() - shot.ms < CAM_SNAPSHOT_MAX_AGE_S * 1000UL; }

// ----------------------------------------------------------------------------
//  SD card (1-bit mode: leaves GPIO 4 alone, so the flash LED stays dark)
// ----------------------------------------------------------------------------
bool mountSd() {
  if (sdOk) return true;
  // format_if_mount_failed = true: a card that cannot be read (unformatted, exFAT, ...) is
  // formatted to FAT. A card that mounts fine keeps its files.
  if (!SD_MMC.begin("/sdcard", true, true)) {
    SD_MMC.end();
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    SD_MMC.end();
    return false;
  }
  if (!SD_MMC.exists(CAM_SD_DIR)) SD_MMC.mkdir(CAM_SD_DIR);
  sdOk = true;
  Serial.printf("SD card mounted: %llu MB, %llu MB used\n",
                SD_MMC.totalBytes() / (1024 * 1024), SD_MMC.usedBytes() / (1024 * 1024));
  return true;
}

// Writes the current `shot` to the card. Returns the file name ("" = not saved).
String saveShotToSd() {
  if (!shot.len || !mountSd()) return "";
  String name;
  if (shot.t) {
    name = timeStr(shot.t, "%Y-%m-%d_%H-%M-%S") + ".jpg";
  } else {                                 // no clock yet -> running number
    prefs.begin("ggcam", false);
    uint32_t n = prefs.getUInt("seq", 0) + 1;
    prefs.putUInt("seq", n);
    prefs.end();
    name = "notime_" + String(n) + ".jpg";
  }
  String path = String(CAM_SD_DIR) + "/" + name;
  File f = SD_MMC.open(path, FILE_WRITE);
  size_t written = f ? f.write(shot.buf, shot.len) : 0;
  if (f) f.close();
  if (written != shot.len) {               // card pulled or full -> remount on the next try
    Serial.printf("SD write failed (%u of %u bytes)\n", (unsigned)written, (unsigned)shot.len);
    if (f) SD_MMC.remove(path);
    SD_MMC.end();
    sdOk = false;
    return "";
  }
  sdSaved++;
  sdLastFile = name;
  Serial.printf("Saved %s (%u bytes)\n", path.c_str(), (unsigned)shot.len);
  return name;
}

// ----------------------------------------------------------------------------
//  Telegram (send only)
// ----------------------------------------------------------------------------
// Uploads `shot`.  1 = Telegram confirmed, 0 = certainly not delivered,
// -1 = fully uploaded but no answer (usually delivered -> do not send again).
int tgSendShot(const String &caption) {
  if (!tgEnabled()) { tgLastResult = "Telegram is not configured (config.h)"; return 0; }
  if (WiFi.status() != WL_CONNECTED) { tgLastResult = "no WiFi"; return 0; }

  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.telegram.org", 443, 10000)) {
    tgLastResult = "connection to Telegram failed";
    return 0;
  }

  const bool asFile = CAM_TELEGRAM_AS_FILE;
  const char *boundary = "GreenGuardianCamBoundary";
  String head;
  head += String("--") + boundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + TELEGRAM_CHAT_ID + "\r\n";
  head += String("--") + boundary + "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption + "\r\n";
  head += String("--") + boundary + "\r\nContent-Disposition: form-data; name=\"" + (asFile ? "document" : "photo") +
          "\"; filename=\"tent.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = String("\r\n--") + boundary + "--\r\n";

  client.print(String("POST /bot") + TELEGRAM_BOT_TOKEN + (asFile ? "/sendDocument" : "/sendPhoto") + " HTTP/1.1\r\n"
               "Host: api.telegram.org\r\n"
               "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
               "Content-Length: " + String(head.length() + shot.len + tail.length()) + "\r\n"
               "Connection: close\r\n\r\n");
  client.print(head);
  for (size_t off = 0; off < shot.len;) {
    size_t n = min((size_t)2048, shot.len - off);
    size_t w = client.write(shot.buf + off, n);
    if (w == 0) {
      client.stop();
      tgLastResult = "upload aborted";
      return 0;
    }
    off += w;
  }
  client.print(tail);

  // wait for {"ok":true ...}
  String resp;
  uint32_t t0 = millis();
  while (millis() - t0 < 20000 && (client.connected() || client.available())) {
    while (client.available() && resp.length() < 2000) resp += (char)client.read();
    if (resp.indexOf("\"ok\":") >= 0) break;
    delay(10);
  }
  client.stop();

  if (resp.indexOf("\"ok\":true") >= 0) {
    tgLastOk = timeValid() ? time(nullptr) : 0;
    tgLastResult = "sent";
    return 1;
  }
  int d = resp.indexOf("\"description\":\"");
  if (d >= 0) {                           // Telegram refused it and says why
    tgLastResult = resp.substring(d + 15, resp.indexOf('"', d + 15));
    return 0;
  }
  tgLastResult = "uploaded, no confirmation";
  return -1;
}

// Fresh picture -> SD card -> Telegram. Same return values as tgSendShot().
int photoJob(const char *title) {
  if (!capture()) { tgLastResult = "capture failed"; return 0; }
  saveShotToSd();
  String caption = String("\xF0\x9F\x93\xB8 ") + title;           // camera emoji
  if (shot.t) caption += " \xC2\xB7 " + timeStr(shot.t, "%d.%m.%Y %H:%M");
  int r = tgSendShot(caption);
  Serial.printf("Telegram photo: %s\n", tgLastResult.c_str());
  return r;
}

void checkDailyPhoto() {
  if (CAM_PHOTO_HOUR < 0 || !timeValid()) return;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  int key = (lt.tm_year + 1900) * 1000 + lt.tm_yday;
  if (lt.tm_hour != CAM_PHOTO_HOUR || key == lastPhotoKey) return;

  // at most 3 tries per day, 5 minutes apart
  if (photoTryKey != key) { photoTryKey = key; photoTries = 0; }
  if (photoTries >= 3 || (photoTries > 0 && millis() - photoTryMs < 5UL * 60UL * 1000UL)) return;
  photoTries++;
  photoTryMs = millis();

  if (photoJob("Daily tent photo") != 0) {
    lastPhotoKey = key;
    prefs.begin("ggcam", false);
    prefs.putInt("photoKey", lastPhotoKey);
    prefs.end();
  }
}

// ----------------------------------------------------------------------------
//  Heartbeat to the sensor ESP32 (UDP broadcast, see cam_link.h)
// ----------------------------------------------------------------------------
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  JsonDocument doc;
  doc["m"] = CAM_LINK_MAGIC;
  doc["rssi"] = WiFi.RSSI();
  doc["up"] = millis() / 1000;
  doc["shot"] = (uint32_t)shot.t;
  doc["bytes"] = (uint32_t)shot.len;
  doc["w"] = shot.w;
  doc["h"] = shot.h;
  doc["refresh"] = CAM_SNAPSHOT_MAX_AGE_S;
  doc["hour"] = CAM_PHOTO_HOUR;
  doc["tg"] = (uint32_t)tgLastOk;
  doc["sd"] = sdOk;
  doc["ok"] = camOk;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  udp.beginPacket(WiFi.broadcastIP(), CAM_LINK_PORT);
  udp.write((const uint8_t *)buf, n);
  udp.endPacket();
}

// ----------------------------------------------------------------------------
//  Web server
// ----------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Green Guardian Cam</title>
<style>body{margin:0;padding:16px;font-family:system-ui,sans-serif;background:#0b140d;color:#e6f2e7;text-align:center}
img{max-width:100%;border-radius:12px;border:1px solid #24402a}a{color:#5fd068}
button{background:#2f7d38;color:#fff;border:0;border-radius:12px;padding:10px 14px;font-weight:600;margin:8px 4px;cursor:pointer}
table{margin:8px auto;border-collapse:collapse;text-align:left;font-size:.9rem}td{padding:4px 8px;border-bottom:1px solid #24402a}
td:last-child{color:#8fae94}input{width:60px;background:#0b140d;color:#e6f2e7;border:1px solid #24402a;border-radius:8px;padding:5px}
#info{color:#8fae94;font-size:.9rem}</style></head><body>
<h2>Green Guardian &middot; Tent Camera</h2>
<img id="i" alt="no picture"><div id="info">loading...</div>
<button onclick="shot(1)">New picture</button>
<button onclick="fetch('/api/telegram-photo',{method:'POST'}).then(r=>r.json()).then(j=>alert(j.msg))">Send to Telegram</button>
<h3>Picture tuning</h3>
<table id="t"></table>
<button onclick="apply()">Apply + new picture</button>
<button onclick="if(confirm('Back to the sensor defaults? The camera restarts.'))fetch('/api/tune?reset=1').then(()=>setTimeout(()=>location.reload(),8000))">Reset</button>
<p><a href="/sd">Photos on the SD card</a> &middot; <a href="/status">Status</a></p>
<script>
const F=[['quality','0 to 63','JPEG compression, lower = better (file must stay below ~384 kB)'],
['sharpness','-1 to 7','-1 = sensor default, 0-6 = manual (higher = sharper), 7 = automatic'],
['gainceiling','0 to 6','max. gain 2x..128x, low = less noise but needs light'],
['ae_level','-2 to 2','exposure: darker .. brighter'],
['aec2','0 or 1','second auto-exposure algorithm of the sensor'],
['saturation','-2 to 2','colour strength'],['contrast','-2 to 2',''],['brightness','-2 to 2','']];
const $=id=>document.getElementById(id);
$('t').innerHTML=F.map(f=>`<tr><td>${f[0]}</td><td><input type="number" id="f_${f[0]}"></td><td>${f[1]}${f[2]?' &middot; '+f[2]:''}</td></tr>`).join('');
function fill(t){for(const f of F)$('f_'+f[0]).value=t[f[0]];}
function info(){fetch('/status').then(r=>r.json()).then(s=>{fill(s.tune);
 $('info').textContent=s.shot.width+'x'+s.shot.height+' \u00b7 '+Math.round(s.shot.bytes/1024)+' kB \u00b7 JPEG quality '+s.jpegQuality+' \u00b7 '+s.sensor;});}
function shot(fresh){$('info').textContent='taking picture...';const i=$('i');i.onload=info;i.onerror=()=>$('info').textContent='no picture';
 i.src='/snapshot.jpg?'+(fresh?'fresh=1&':'')+'t='+Date.now();}
function apply(){fetch('/api/tune?'+F.map(f=>f[0]+'='+encodeURIComponent($('f_'+f[0]).value)).join('&')).then(r=>r.json()).then(()=>shot(1));}
shot(0);
</script>
</body></html>)rawliteral";

void tuneJson(JsonObject t);

void sendJson(int code, const String &body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

void tuneJson(JsonObject t) {
  t["brightness"] = tune.brightness;
  t["contrast"] = tune.contrast;
  t["saturation"] = tune.saturation;
  t["ae_level"] = tune.ae_level;
  t["aec2"] = tune.aec2;
  t["gainceiling"] = tune.gainceiling;
  t["sharpness"] = tune.sharpness;
  t["quality"] = tune.quality;
}

// GET /api/tune                      -> current values
// GET /api/tune?sharpness=4&...      -> change, store and apply (takes effect with the next picture)
// GET /api/tune?reset=1              -> back to the driver defaults (restarts the camera)
void handleTune() {
  if (server.hasArg("reset")) {
    prefs.begin("ggcam", false);
    for (const char *k : {"tBri", "tCon", "tSat", "tAe", "tAec2", "tGc", "tSha", "tQ"}) prefs.remove(k);
    prefs.end();
    sendJson(200, "{\"ok\":true,\"msg\":\"Defaults restored - camera restarts\"}");
    delay(300);
    ESP.restart();
  }
  bool changed = false;
  auto take = [&](const char *key, int &v, int lo, int hi) {
    if (!server.hasArg(key)) return;
    v = constrain((int)server.arg(key).toInt(), lo, hi);
    changed = true;
  };
  take("brightness", tune.brightness, -2, 2);
  take("contrast", tune.contrast, -2, 2);
  take("saturation", tune.saturation, -2, 2);
  take("ae_level", tune.ae_level, -2, 2);
  take("aec2", tune.aec2, 0, 1);
  take("gainceiling", tune.gainceiling, 0, 6);
  take("sharpness", tune.sharpness, -1, 7);
  take("quality", tune.quality, 0, 63);
  if (changed) { saveTune(); applyTune(); }
  JsonDocument doc;
  doc["ok"] = true;
  doc["msg"] = changed ? "Saved" : "Current values";
  tuneJson(doc["tune"].to<JsonObject>());
  String out;
  serializeJson(doc, out);
  sendJson(200, out);
}

void handleSnapshot() {
  if (server.hasArg("fresh") || !shotFresh()) capture();   // on failure the previous picture is served
  if (!shot.len) return sendJson(503, "{\"ok\":false,\"msg\":\"Camera not ready\"}");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (PGM_P)shot.buf, shot.len);
}

void handleStatus() {
  JsonDocument doc;
  JsonObject d = doc.to<JsonObject>();
  d["camera"] = camOk;
  d["sensor"] = sensorName;
  d["psram"] = psramFound();
  JsonObject s = d["shot"].to<JsonObject>();
  s["time"] = (uint32_t)shot.t;
  s["ageS"] = shot.len ? (millis() - shot.ms) / 1000 : 0;
  s["bytes"] = (uint32_t)shot.len;
  s["width"] = shot.w;
  s["height"] = shot.h;
  d["captureFails"] = captureFails;
  d["jpegQuality"] = jpegQ;
  tuneJson(d["tune"].to<JsonObject>());
  d["photoHour"] = CAM_PHOTO_HOUR;
  d["telegram"] = tgEnabled();
  d["tgLastOk"] = (uint32_t)tgLastOk;
  d["tgLastResult"] = tgLastResult;
  JsonObject sd = d["sd"].to<JsonObject>();
  sd["mounted"] = sdOk;
  if (sdOk) {
    sd["totalMB"] = (uint32_t)(SD_MMC.totalBytes() / (1024 * 1024));
    sd["usedMB"] = (uint32_t)(SD_MMC.usedBytes() / (1024 * 1024));
  }
  sd["savedSinceBoot"] = sdSaved;
  sd["lastFile"] = sdLastFile;
  d["now"] = timeValid() ? (uint32_t)time(nullptr) : 0;
  d["uptime"] = millis() / 1000;
  d["rssi"] = WiFi.RSSI();
  d["ip"] = WiFi.localIP().toString();
  d["freeHeap"] = ESP.getFreeHeap();
  d["freePsram"] = ESP.getFreePsram();
  d["resetReason"] = resetReasonText();
  String out;
  serializeJsonPretty(doc, out);
  sendJson(200, out);
}

void handleTelegramPhoto() {
  if (!tgEnabled()) return sendJson(400, "{\"ok\":false,\"msg\":\"Telegram is not configured (config.h)\"}");
  tgRequested = true;
  sendJson(200, "{\"ok\":true,\"msg\":\"Photo is on its way to Telegram\"}");
}

// File names on the card are generated by saveShotToSd(); accept only that pattern.
bool safeName(const String &n) {
  if (n.length() < 5 || n.length() > 40 || !n.endsWith(".jpg")) return false;
  for (char ch : n) if (!isalnum((unsigned char)ch) && ch != '-' && ch != '_' && ch != '.') return false;
  return n.indexOf("..") < 0;
}

void handleSdList() {
  if (!mountSd()) return server.send(200, "text/html", "<p>No SD card.</p>");
  File dir = SD_MMC.open(CAM_SD_DIR);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent("<!DOCTYPE html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                     "<body style='font-family:system-ui;background:#0b140d;color:#e6f2e7'><h3>Photos on the SD card</h3>");
  String chunk;
  int count = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    n = n.substring(n.lastIndexOf('/') + 1);
    if (!f.isDirectory() && safeName(n)) {
      chunk += "<a style='color:#5fd068' href='/sd/file?name=" + n + "'>" + n + "</a> (" + String(f.size() / 1024) + " kB)<br>";
      count++;
    }
    f.close();
    if (chunk.length() > 1024) { server.sendContent(chunk); chunk = ""; }
  }
  chunk += "<p>" + String(count) + " photos</p>";
  server.sendContent(chunk);
  server.sendContent("");
}

void handleSdFile() {
  String n = server.arg("name");
  if (!safeName(n) || !mountSd()) return server.send(404, "text/plain", "not found");
  File f = SD_MMC.open(String(CAM_SD_DIR) + "/" + n, FILE_READ);
  if (!f) return server.send(404, "text/plain", "not found");
  server.streamFile(f, "image/jpeg");
  f.close();
}

void setupWeb() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/snapshot.jpg", HTTP_GET, handleSnapshot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/api/tune", HTTP_ANY, handleTune);
  server.on("/api/telegram-photo", HTTP_POST, handleTelegramPhoto);
  server.on("/sd", HTTP_GET, handleSdList);
  server.on("/sd/file", HTTP_GET, handleSdFile);
  server.onNotFound([] { server.send(404, "text/plain", "404"); });
  server.begin();
}

// ----------------------------------------------------------------------------
//  WiFi
// ----------------------------------------------------------------------------
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(CAM_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
  if (WiFi.status() == WL_CONNECTED) Serial.printf("WiFi connected: http://%s\n", WiFi.localIP().toString().c_str());
  else Serial.println("WiFi not connected yet - retrying in the background");
}

void maintainWifi() {
  static uint32_t lostSince = 0, lastTry = 0;
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) { lostSince = 0; return; }
  if (!lostSince) { lostSince = now; lastTry = now; Serial.println("WiFi lost"); }
  if (now - lastTry >= 20000) {                       // retry every 20 s
    lastTry = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (now - lostSince >= 10UL * 60UL * 1000UL) {      // 10 min without WiFi -> full restart
    Serial.println("WiFi down for 10 min - restarting");
    delay(200);
    ESP.restart();
  }
}

// ----------------------------------------------------------------------------
//  Setup / loop
// ----------------------------------------------------------------------------
void setup() {
  pinMode(PIN_FLASH_LED, OUTPUT);
  digitalWrite(PIN_FLASH_LED, LOW);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);   // off

  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Green Guardian Cam ===");
  Serial.printf("Reset reason: %s\n", resetReasonText());

  prefs.begin("ggcam", true);
  lastPhotoKey = prefs.getInt("photoKey", 0);
  prefs.end();

  camOk = initCamera();
  Serial.printf("Camera: %s (%s)\n", camOk ? "ok" : "FAILED", sensorName);
  if (camOk) { loadTune(); applyTune(); }

  setupWifi();
  configTzTime(TZ_INFO, NTP_SERVER);

  ArduinoOTA.setHostname(CAM_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  MDNS.addService("http", "tcp", 80);

  setupWeb();
}

void loop() {
  static uint32_t tWifi = 0, tBeat = 0, tDaily = 0, tCamRetry = 0;
  static bool sdTried = false;
  uint32_t now = millis();

  server.handleClient();
  ArduinoOTA.handle();

  if (now - tWifi >= 10000) { tWifi = now; maintainWifi(); }
  if (now - tBeat >= CAM_HEARTBEAT_S * 1000UL || tBeat == 0) { tBeat = now ? now : 1; sendHeartbeat(); }
  if (now - tDaily >= 30000) { tDaily = now; checkDailyPhoto(); }
  // SD card only after the network is up: formatting an unreadable card can take a while
  // and must not keep the camera off the network.
  if (!sdTried && tBeat) {
    sdTried = true;
    if (!mountSd()) Serial.println("No SD card - photos are only sent, not stored");
    sendHeartbeat();
  }
  if (tgRequested) { tgRequested = false; photoJob("Tent photo"); sendHeartbeat(); }

  // camera did not start (loose ribbon cable?) -> try again every minute
  if (!camOk && now - tCamRetry >= 60000) {
    tCamRetry = now;
    camOk = initCamera();
    if (camOk) { loadTune(); applyTune(); }
  }

  delay(2);   // let the idle task run -> cooler chip
}
