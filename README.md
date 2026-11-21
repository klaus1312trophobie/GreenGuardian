# 🌿 Green Guardian

ESP32 grow monitor: measures soil moisture, air temperature and humidity, shows them on an
OLED and a web dashboard, and sends alerts and reports via Telegram.

- Web dashboard with live values, VPD, dew point, heat index, 24 h history, min/max
- 10 s rolling averages
- Telegram bot: dry-soil / too hot / too cold alerts, daily report, `/status`, `/watered`, `/help`
- Automatic watering detection (sudden rise in soil moisture)
- OLED with rotating pages (mounted upside down, `setRotation(2)`)
- OTA updates over WiFi, settings stored in NVS (survive updates and reboots)
- Optional tent camera (ESP32-CAM): picture in the dashboard, daily Telegram photo, SD archive – see [Tent camera](#tent-camera)
- Android home-screen widget: [PflanzenWidget](https://github.com/klaus1312trophobie/PflanzenWidget)

## Parts list

| # | Part | Notes |
|---|------|-------|
| 1 | ESP32 DevKit (ESP-WROOM-32, CP2102 USB-serial) | any ESP32 dev board with GPIO 21/22/32/34 works |
| 1 | DHT22 (AM2302) temperature & humidity sensor | module with 3 pins has the pull-up built in; bare 4-pin sensor needs a 10 kΩ pull-up between VCC and DATA |
| 1 | Capacitive soil moisture sensor v1.0 | analog output; the *capacitive* type, not the corroding resistive one |
| 1 | 0.96" OLED display, SSD1306, 128×64, I²C | address `0x3C` |
| – | Jumper wires / breadboard (or solder + perfboard) | |
| 1 | Micro-USB cable + 5 V USB power supply | |

## Wiring

| Part | Pin | ESP32 |
|------|-----|-------|
| DHT22 | VCC (+) | 3V3 |
| DHT22 | DATA (OUT) | **GPIO 32** |
| DHT22 | GND (−) | GND |
| Soil sensor | VCC | 3V3 |
| Soil sensor | AOUT | **GPIO 34** (ADC1, input only) |
| Soil sensor | GND | GND |
| OLED SSD1306 | VCC | 3V3 |
| OLED SSD1306 | SDA | **GPIO 21** |
| OLED SSD1306 | SCL | **GPIO 22** |
| OLED SSD1306 | GND | GND |

All parts run at 3.3 V, so no level shifting is needed. Soil sensor on 3.3 V keeps its
output within the ESP32 ADC range.

### Schematic

```mermaid
flowchart LR
    subgraph ESP32["ESP32 DevKit"]
        V33["3V3"]
        GND["GND"]
        G32["GPIO 32"]
        G34["GPIO 34 (ADC1)"]
        G21["GPIO 21 (SDA)"]
        G22["GPIO 22 (SCL)"]
    end

    subgraph DHT["DHT22"]
        D_VCC["VCC"]
        D_DATA["DATA"]
        D_GND["GND"]
    end

    subgraph SOIL["Capacitive soil sensor v1.0"]
        S_VCC["VCC"]
        S_AOUT["AOUT"]
        S_GND["GND"]
    end

    subgraph OLED["OLED SSD1306 0.96 in (I2C 0x3C)"]
        O_VCC["VCC"]
        O_SDA["SDA"]
        O_SCL["SCL"]
        O_GND["GND"]
    end

    V33 --- D_VCC
    V33 --- S_VCC
    V33 --- O_VCC
    GND --- D_GND
    GND --- S_GND
    GND --- O_GND
    G32 --- D_DATA
    G34 --- S_AOUT
    G21 --- O_SDA
    G22 --- O_SCL
```

Same thing as plain text:

```
                    ┌─────────────────────┐
   DHT22            │                     │            OLED SSD1306
  ┌───────┐         │       ESP32         │         ┌────────────────┐
  │ VCC   ├─────────┤ 3V3                 │         │                │
  │ DATA  ├─────────┤ GPIO 32             │         │                │
  │ GND   ├─────────┤ GND                 │         │                │
  └───────┘         │                     │         │ VCC ───── 3V3  │
                    │            GPIO 21  ├─────────┤ SDA            │
   Soil sensor      │            GPIO 22  ├─────────┤ SCL            │
  ┌───────┐         │                     │         │ GND ───── GND  │
  │ VCC   ├─────────┤ 3V3                 │         └────────────────┘
  │ AOUT  ├─────────┤ GPIO 34 (ADC1)      │
  │ GND   ├─────────┤ GND                 │
  └───────┘         └──────────┬──────────┘
                               │ USB 5 V
```

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Copy the config template and fill in your values (WiFi, Telegram token + chat ID, OTA password):
   ```
   cp include/config.example.h include/config.h
   ```
   `include/config.h` is git-ignored because it contains secrets.
3. First flash over USB:
   ```
   pio run -e esp32dev_usb -t upload
   ```
4. After that, update over WiFi (OTA):
   ```
   pio run -t upload
   ```
   OTA uses a fixed host port (8266) so the firewall can allow it, e.g.
   `sudo ufw allow from 192.168.178.0/24 to any port 8266 proto tcp`.
5. Open `http://greenguardian.local` (or the IP shown on the OLED).

Calibration (dry / wet values), alert threshold and report hour are set in the dashboard
and stored in flash; they are kept across OTA updates.

## API

`GET /data` returns JSON, e.g.:

```json
{"moisture":87,"temp":25.7,"humidity":61.3,"vpd":0.91,"vpdStage":"vegetative sweet spot",
 "minmax":{"tempMin":24.3,"tempMax":25.8,"humMin":53.6,"humMax":61.3,"soilMin":86,"soilMax":100},
 "threshold":40,"lastWatered":1791899397,"rssi":-84}
```

The keys `moisture`, `temp` and `humidity` are used by the Android widget and must stay stable.
`GET /history` returns the 24 h history.

## Telegram commands

| Command | Action |
|---------|--------|
| `/status` | current values, min/max, last watering |
| `/watered` | log a watering manually |
| `/photo` | picture from the tent camera (if installed) |
| `/help` | command list and dashboard link |

## Tent camera

Optional second board: an AI-Thinker ESP32-CAM (OV2640) running the firmware in `cam/`.
It reads WiFi, Telegram and OTA settings from the same `include/config.h`.

- Full-resolution still (1600×1200) on demand – no video stream, so the sensor stays cool
- Daily photo to the Telegram chat at 13:00 (`CAM_PHOTO_HOUR`); every photo sent to
  Telegram is also stored on the SD card in `/tent/` (a card that cannot be read is formatted)
- Dashboard card with the current picture (refreshed every 10 min, `CAM_SNAPSHOT_MAX_AGE_S`),
  "New picture", "Send to Telegram" and a link to the SD card photos
- `/photo` in Telegram: the sensor ESP32 receives the command and asks the camera for a picture
- Flash LED (GPIO 4) and status LED stay off

How the two boards work together: the camera broadcasts a small UDP heartbeat every 20 s
(port 4210). The sensor ESP32 learns the camera's IP from it and publishes it in `/data`
(`cam` object); the browser then loads the picture directly from the camera. Only the
sensor ESP32 polls Telegram – the camera just sends – so the two never steal each other's
bot updates.

Flash (ESP32-CAM on the ESP32-CAM-MB USB board):

```
cd cam
pio run -e esp32cam_usb -t upload     # first time, via USB
pio run -e esp32cam -t upload         # later, via WiFi (OTA)
```

Camera endpoints (`http://greenguardian-cam.local`):

| Path | Action |
|------|--------|
| `/` | picture + buttons |
| `/snapshot.jpg` | last picture, re-taken if older than 10 min (`?fresh=1` = take a new one now) |
| `/status` | JSON status (sensor, SD card, last Telegram result, reset reason) |
| `/api/tune` | picture tuning without re-flashing: `?sharpness=4&gainceiling=0&ae_level=0&aec2=1&saturation=0&contrast=0&brightness=0&quality=4`, `?reset=1` = sensor defaults; also on the camera's own page |
| `/sd` | photos stored on the SD card |
| `POST /api/telegram-photo` | take a picture, store it on the SD card and send it to Telegram |

Orientation: set `CAM_VFLIP` / `CAM_HMIRROR` in `config.h` if the picture is upside down or mirrored.

Printable case (PETG) and 19 mm tent-pole clip: see `cam/case/`.
