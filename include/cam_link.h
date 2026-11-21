#pragma once
// =============================================================================
//  Green Guardian – link between the sensor ESP32 (src/) and the tent camera
//  (cam/). Both firmwares include this file. Every value can be overridden in
//  config.h (see config.example.h).
// =============================================================================

#ifndef CAM_HOSTNAME
#define CAM_HOSTNAME           "greenguardian-cam"   // -> http://greenguardian-cam.local
#endif

// The camera broadcasts a small UDP heartbeat; the sensor ESP32 learns the
// camera's IP from it. No fixed IPs and no name lookups needed.
#ifndef CAM_LINK_PORT
#define CAM_LINK_PORT          4210
#endif
#ifndef CAM_HEARTBEAT_S
#define CAM_HEARTBEAT_S        20
#endif
#define CAM_LINK_MAGIC         "GGCAM1"
#define CAM_OFFLINE_AFTER_S    (CAM_HEARTBEAT_S * 4)  // no heartbeat for this long = offline

// --- Camera behaviour ---
#ifndef CAM_PHOTO_HOUR
#define CAM_PHOTO_HOUR         13    // daily Telegram photo at 13:00, -1 = off
#endif
#ifndef CAM_SNAPSHOT_MAX_AGE_S
#define CAM_SNAPSHOT_MAX_AGE_S 600   // dashboard picture is refreshed at most every 10 min
#endif
#ifndef CAM_JPEG_QUALITY
#define CAM_JPEG_QUALITY       5     // 0-63, lower = better quality / bigger file
#endif
#ifndef CAM_VFLIP
#define CAM_VFLIP              0     // 1 = picture upside down -> flip
#endif
#ifndef CAM_HMIRROR
#define CAM_HMIRROR            0     // 1 = mirror horizontally
#endif
#ifndef CAM_TELEGRAM_AS_FILE
#define CAM_TELEGRAM_AS_FILE   0     // 1 = send as document (Telegram keeps the original JPEG untouched)
#endif
#ifndef CAM_SD_DIR
#define CAM_SD_DIR             "/tent"  // folder on the SD card for the Telegram photos
#endif
