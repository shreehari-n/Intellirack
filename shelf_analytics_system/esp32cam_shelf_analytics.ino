/*
 * ============================================================
 *  SHELF ANALYTICS SYSTEM — ESP32-CAM
 *  Hardware: AI-Thinker ESP32-CAM (OV2640)
 *  Features: Motion detection via frame diff, WiFi, REST API
 * ============================================================
 *
 *  REQUIRED LIBRARIES (Arduino Library Manager):
 *  - ESP32 board package (espressif/arduino-esp32)
 *  - ArduinoJson  (Benoit Blanchon) — v6.x
 *  - HTTPClient   (built-in with ESP32)
 *
 *  WIRING (AI-Thinker ESP32-CAM):
 *  ┌──────────────────────────────────────────┐
 *  │ No external wiring needed for camera     │
 *  │                                          │
 *  │ Optional: PIR on GPIO 13 (solder pad)    │
 *  │   PIR OUT → GPIO13                       │
 *  │   PIR VCC → 3.3V or 5V                  │
 *  │                                          │
 *  │ Optional: Vibration sensor on GPIO 12    │
 *  │   VIB OUT → GPIO12 (INPUT_PULLUP)        │
 *  │                                          │
 *  │ Flash LED : GPIO4 (built-in, HIGH=ON)    │
 *  └──────────────────────────────────────────┘
 *
 *  DETECTION PIPELINE:
 *  OV2640 → frame diff → motion score → presence state
 *  → dwell timer → interest / pickup / cart inference
 *  → REST POST to your backend every session end
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_timer.h"

// ─── WiFi & Server Config ──────────────────────────────────
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASS     = "YOUR_PASSWORD";
const char* API_ENDPOINT  = "http://192.168.1.100:5000/api/shelf-event";  // Your server
const char* DEVICE_ID     = "SHELF-01";  // Unique per shelf

// ─── Camera Pins (AI-Thinker module) ─────────────────────
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ─── Optional Sensor Pins ─────────────────────────────────
#define PIR_PIN           13   // GPIO13 — optional PIR
#define VIBRATION_PIN     12   // GPIO12 — optional vibration
#define FLASH_LED_PIN      4   // Built-in flash

// ─── Detection Thresholds ─────────────────────────────────
#define MOTION_PIXEL_THRESHOLD   20    // Min pixel diff to count as changed
#define MOTION_BLOCK_THRESHOLD   50    // Min changed blocks = motion detected
#define INTEREST_THRESHOLD_MS    5000  // 5s dwell = interested
#define SESSION_TIMEOUT_MS       6000  // 6s no motion = session end
#define CART_WINDOW_MS           3000  // Pickup → leaves in 3s = cart add
#define FRAME_INTERVAL_MS        150   // Capture frame every 150ms
#define REPORT_QUEUE_SIZE        10    // Buffer sessions before WiFi send

// ─── State Machine ─────────────────────────────────────────
enum ShelfState { IDLE, DETECTED, INTERESTED, PICKUP, LEAVING };
ShelfState state = IDLE;

// ─── Metrics ───────────────────────────────────────────────
struct Metrics {
  uint32_t footfall       = 0;
  uint32_t interested     = 0;
  uint32_t pickups        = 0;
  uint32_t addToCart      = 0;
  uint64_t totalDwellMs   = 0;
  uint32_t sessions       = 0;
} metrics;

struct Session {
  uint32_t startTime;
  uint32_t dwellMs;
  bool     interested;
  bool     pickedUp;
  bool     addedToCart;
};

// ─── Runtime Variables ─────────────────────────────────────
Session   activeSession;
uint8_t*  prevFrame     = nullptr;
bool      motionActive  = false;
uint32_t  lastMotionMs  = 0;
uint32_t  lastFrameMs   = 0;
uint32_t  pickupTime    = 0;

// Queue for offline buffering
Session   sessionQueue[REPORT_QUEUE_SIZE];
uint8_t   queueHead = 0;
uint8_t   queueSize = 0;

// ───────────────────────────────────────────────────────────
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;  // 10MHz for stability
  config.pixel_format = PIXFORMAT_GRAYSCALE;  // Grayscale = fast diff
  // Use smallest frame for motion detection — saves RAM
  config.frame_size   = FRAMESIZE_QVGA;  // 320×240
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  // Reduce sharpness, enable AGC for indoor low light
  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 1);
  s->set_saturation(s, -2);
  s->set_gainceiling(s, (gainceiling_t)6);

  Serial.println("Camera OK");
  return true;
}

// ─── Frame differencing motion detection ──────────────────
// Divides 320×240 into 8×8 pixel blocks, counts changed blocks
int detectMotion(camera_fb_t* fb) {
  if (!fb || fb->format != PIXFORMAT_GRAYSCALE) return 0;

  size_t len = fb->len;
  uint8_t* curr = fb->buf;

  if (!prevFrame) {
    // First frame — allocate and copy, no motion
    prevFrame = (uint8_t*)malloc(len);
    if (prevFrame) memcpy(prevFrame, curr, len);
    return 0;
  }

  int w = fb->width;
  int h = fb->height;
  int blockW = 8, blockH = 8;
  int changedBlocks = 0;

  for (int by = 0; by < h; by += blockH) {
    for (int bx = 0; bx < w; bx += blockW) {
      int diffSum = 0;
      int count   = 0;
      for (int dy = 0; dy < blockH && (by + dy) < h; dy++) {
        for (int dx = 0; dx < blockW && (bx + dx) < w; dx++) {
          int idx = (by + dy) * w + (bx + dx);
          diffSum += abs((int)curr[idx] - (int)prevFrame[idx]);
          count++;
        }
      }
      if (count > 0 && (diffSum / count) > MOTION_PIXEL_THRESHOLD) {
        changedBlocks++;
      }
    }
  }

  memcpy(prevFrame, curr, len);
  return changedBlocks;
}

// ─── Session management ────────────────────────────────────
void startSession() {
  activeSession = {millis(), 0, false, false, false};
  metrics.footfall++;
  state = DETECTED;
  Serial.printf("{\"event\":\"detected\",\"footfall\":%d}\n", metrics.footfall);
}

void recordPickup() {
  activeSession.pickedUp = true;
  pickupTime = millis();
  metrics.pickups++;
  state = PICKUP;
  Serial.println("{\"event\":\"pickup\"}");
}

void endSession() {
  activeSession.dwellMs    = millis() - activeSession.startTime;
  activeSession.interested = (activeSession.dwellMs >= INTEREST_THRESHOLD_MS);

  if (activeSession.interested) metrics.interested++;
  if (activeSession.addedToCart) metrics.addToCart++;
  metrics.totalDwellMs += activeSession.dwellMs;
  metrics.sessions++;

  Serial.printf("{\"event\":\"session_end\",\"dwell_ms\":%d,\"interested\":%s,\"picked_up\":%s,\"added_to_cart\":%s}\n",
    activeSession.dwellMs,
    activeSession.interested  ? "true" : "false",
    activeSession.pickedUp    ? "true" : "false",
    activeSession.addedToCart ? "true" : "false");

  // Queue for async WiFi send
  if (queueSize < REPORT_QUEUE_SIZE) {
    sessionQueue[(queueHead + queueSize) % REPORT_QUEUE_SIZE] = activeSession;
    queueSize++;
  }

  state = IDLE;
}

// ─── WiFi reporting ────────────────────────────────────────
void sendQueuedSessions() {
  if (!WiFi.isConnected() || queueSize == 0) return;

  HTTPClient http;
  http.begin(API_ENDPOINT);
  http.addHeader("Content-Type", "application/json");

  while (queueSize > 0) {
    Session& s = sessionQueue[queueHead];

    StaticJsonDocument<256> doc;
    doc["device_id"]     = DEVICE_ID;
    doc["ts"]            = millis();
    doc["dwell_ms"]      = s.dwellMs;
    doc["interested"]    = s.interested;
    doc["picked_up"]     = s.pickedUp;
    doc["added_to_cart"] = s.addedToCart;
    // Cumulative
    doc["footfall"]      = metrics.footfall;
    doc["total_interested"] = metrics.interested;
    doc["total_pickups"] = metrics.pickups;
    doc["total_cart"]    = metrics.addToCart;
    doc["avg_dwell_ms"]  = metrics.sessions > 0
                           ? (metrics.totalDwellMs / metrics.sessions) : 0;

    String payload;
    serializeJson(doc, payload);

    int code = http.POST(payload);
    if (code > 0) {
      Serial.printf("POST OK: %d\n", code);
    } else {
      Serial.printf("POST failed: %s\n", http.errorToString(code).c_str());
      break;  // Retry next cycle
    }

    queueHead = (queueHead + 1) % REPORT_QUEUE_SIZE;
    queueSize--;
  }

  http.end();
}

// ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN,       INPUT);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("FATAL: Camera failed. Halting.");
    while(1) delay(1000);
  }

  Serial.println("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500); Serial.print(".");
    tries++;
  }
  if (WiFi.isConnected()) {
    Serial.printf("\nWiFi OK: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi failed — offline mode, serial only");
  }

  Serial.printf("{\"event\":\"ready\",\"device\":\"%s\"}\n", DEVICE_ID);
}

// ───────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── 1. Read optional sensors ────────────────────────────
  bool pirDetected    = (digitalRead(PIR_PIN) == HIGH);
  bool vibDetected    = (digitalRead(VIBRATION_PIN) == LOW);

  // ── 2. Camera-based motion detection ───────────────────
  bool cameraMotion   = false;
  if (now - lastFrameMs > FRAME_INTERVAL_MS) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
      int blocks = detectMotion(fb);
      cameraMotion = (blocks > MOTION_BLOCK_THRESHOLD);
      esp_camera_fb_return(fb);
    }
    lastFrameMs = now;
  }

  // Combine camera + PIR (OR logic — either triggers presence)
  bool presenceDetected = cameraMotion || pirDetected;

  if (presenceDetected) lastMotionMs = now;
  bool timeout = (now - lastMotionMs) > SESSION_TIMEOUT_MS;

  // ── 3. State Machine ────────────────────────────────────
  switch (state) {

    case IDLE:
      if (presenceDetected) startSession();
      break;

    case DETECTED:
      if (timeout) { endSession(); break; }
      if (vibDetected) { recordPickup(); break; }
      if ((now - activeSession.startTime) >= INTEREST_THRESHOLD_MS) {
        state = INTERESTED;
        Serial.printf("{\"event\":\"interested\",\"dwell_ms\":%d}\n",
                      now - activeSession.startTime);
      }
      break;

    case INTERESTED:
      if (timeout) { endSession(); break; }
      if (vibDetected) recordPickup();
      break;

    case PICKUP:
      if (!presenceDetected) {
        // Customer left after pickup = added to cart
        if ((now - pickupTime) < CART_WINDOW_MS) {
          activeSession.addedToCart = true;
          Serial.println("{\"event\":\"add_to_cart\"}");
        }
        endSession();
      } else if (timeout) {
        // Still there too long = maybe put back
        endSession();
      }
      break;

    case LEAVING:
      endSession();
      break;
  }

  // ── 4. Send queued sessions over WiFi (non-blocking) ───
  // Only send when IDLE to avoid interference with session timing
  static uint32_t lastSend = 0;
  if (state == IDLE && queueSize > 0 && (now - lastSend > 2000)) {
    sendQueuedSessions();
    lastSend = now;
  }

  // Yield to RTOS
  delay(10);
}

/*
 * ============================================================
 *  REST API PAYLOAD (POST to API_ENDPOINT):
 *  {
 *    "device_id": "SHELF-01",
 *    "ts": 123456,
 *    "dwell_ms": 7300,
 *    "interested": true,
 *    "picked_up": true,
 *    "added_to_cart": true,
 *    "footfall": 12,
 *    "total_interested": 8,
 *    "total_pickups": 5,
 *    "total_cart": 4,
 *    "avg_dwell_ms": 6100
 *  }
 *
 *  PYTHON RECEIVER (quick test server):
 *  $ pip install flask
 *  $ python3 -c "
 *  from flask import Flask,request; app=Flask(__name__)
 *  @app.route('/api/shelf-event',methods=['POST'])
 *  def ev(): print(request.json); return 'OK'
 *  app.run(host='0.0.0.0',port=5000)
 *  "
 * ============================================================
 */
