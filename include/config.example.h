#pragma once
// ======================= Green Guardian – configuration =======================

// --- WiFi ---
#define WIFI_SSID          "YOUR_WIFI_SSID"
#define WIFI_PASSWORD      "YOUR_WIFI_PASSWORD"
#define HOSTNAME           "greenguardian"      // -> http://greenguardian.local
#define AP_SSID            "GreenGuardian-Setup" // fallback access point if WiFi fails
#define AP_PASSWORD        "growgreen420"
#define OTA_PASSWORD       "growgreen"

// --- Telegram ---
// Token from @BotFather. Chat ID: send /start to your bot, it replies with your ID.
#define TELEGRAM_BOT_TOKEN ""
#define TELEGRAM_CHAT_ID   ""

// --- Time (Germany incl. daylight saving) ---
#define TZ_INFO            "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER         "pool.ntp.org"

// --- Pins ---
#define PIN_DHT            32
#define PIN_SOIL           34   // ADC1, input-only – perfect for analog
#define I2C_SDA            21
#define I2C_SCL            22
#define OLED_ADDR          0x3C

// --- Defaults (changeable in the web UI, stored in flash) ---
#define DEFAULT_ALERT_THRESHOLD 40   // % soil moisture -> "water me" alert
#define DEFAULT_REPORT_HOUR     9    // daily Telegram report at 09:00
#define DEFAULT_DRY_RAW         3000 // ADC value in dry soil / air  = 0 %
#define DEFAULT_WET_RAW         1300 // ADC value right after watering = 100 %

// --- Climate alerts (cannabis likes 20–30 °C) ---
#define TEMP_HIGH_ALERT    32.0f
#define TEMP_LOW_ALERT     15.0f
#define LEAF_TEMP_OFFSET   2.0f      // leaf is ~2 °C cooler than air (for VPD)
