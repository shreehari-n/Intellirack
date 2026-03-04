# Shelf Analytics System — Architect README

**Version:** 1.0  
**Date:** March 2026  
**Purpose:** Lightweight retail shelf intelligence — customer detection, dwell tracking, pickup & cart conversion metrics

---

## System Overview

A three-tier edge-to-dashboard analytics pipeline that monitors customer interactions at a retail shelf in real time, with no cloud dependency required.

```
[Shelf Hardware] ──► [Bridge / REST Server] ──► [React Dashboard]
 Arduino Uno              shelf_server.py          shelf_dashboard.jsx
 ESP32-CAM               Flask · Python 3          React · Tailwind
```

---

## File Manifest

| File | Role | Target |
|---|---|---|
| `arduino_uno_shelf_analytics.ino` | Edge firmware — PIR + ultrasonic + vibration | Arduino Uno |
| `esp32cam_shelf_analytics.ino` | Edge firmware — camera frame diff + WiFi POST | ESP32-CAM (AI-Thinker) |
| `shelf_server.py` | Serial bridge + REST API receiver | Any Python 3 host / Raspberry Pi |
| `shelf_dashboard.jsx` | Live metrics dashboard | Browser (React) |

---

## Hardware Bill of Materials

### Option A — Arduino Uno (wired serial output)
| Component | Part | Qty |
|---|---|---|
| Microcontroller | Arduino Uno R3 | 1 |
| Motion sensor | HC-SR501 PIR | 1 |
| Distance sensor | HC-SR04 Ultrasonic | 1 |
| Shelf disturbance | SW-420 Vibration sensor | 1 |
| Resistors / jumpers | — | — |

**Pin mapping:**

```
PIR OUT      → D2
TRIG         → D7
ECHO         → D8
VIBRATION    → D3  (INPUT_PULLUP)
```

### Option B — ESP32-CAM (WiFi, camera-based)
| Component | Part | Qty |
|---|---|---|
| Module | AI-Thinker ESP32-CAM | 1 |
| Programmer | FTDI FT232RL (3.3V) | 1 |
| Optional PIR | HC-SR501 | 1 → GPIO13 |
| Optional vibration | SW-420 | 1 → GPIO12 |

> **Note:** ESP32-CAM has no USB port. Flash via FTDI with IO0 pulled LOW.

---

## Detection Logic

### State Machine (both platforms)

```
IDLE ──[presence]──► DETECTED ──[dwell > 5s]──► INTERESTED
                         │                            │
                    [vibration]                  [vibration]
                         └──────────► PICKUP ◄────────┘
                                         │
                              [customer leaves < 3s]
                                         │
                                    ADD TO CART
                                         │
                                    SESSION END
```

### Presence Detection

| Platform | Method | Threshold |
|---|---|---|
| Arduino Uno | PIR + ultrasonic | Distance < 80 cm AND PIR HIGH |
| ESP32-CAM | Frame differencing (8×8 blocks) + optional PIR | > 50 changed blocks OR PIR HIGH |

### Metric Definitions

| Metric | Definition |
|---|---|
| **Footfall** | Unique customer presence events (PIR/camera trigger) |
| **Interested** | Session dwell time ≥ 5 seconds |
| **Picked Up** | Vibration sensor triggered during session |
| **Added to Cart** | Customer departs within 3 seconds of pickup |
| **Avg Dwell Time** | Mean session duration across all completed sessions |
| **Interest Rate** | interested / footfall × 100 |
| **Pickup Rate** | pickups / footfall × 100 |
| **Cart Rate** | add_to_cart / footfall × 100 |

---

## Data Flow

### Arduino Uno → Server
Serial port outputs newline-delimited JSON at **115200 baud**:
```json
{"event":"customer_detected","footfall":3,"distance_cm":42.5}
{"event":"customer_interested","dwell_ms":5120}
{"event":"item_pickup_detected"}
{"event":"item_added_to_cart"}
{"event":"session_end","dwell_ms":8300,"interested":true,"picked_up":true,"added_to_cart":true}
```
The Python server reads this via `pyserial` and ingests it into the same store.

### ESP32-CAM → Server
HTTP POST to `http://<server>:5000/api/shelf-event` after each session:
```json
{
  "device_id": "SHELF-01",
  "ts": 1234567890,
  "dwell_ms": 7300,
  "interested": true,
  "picked_up": true,
  "added_to_cart": false,
  "footfall": 12,
  "total_interested": 8,
  "total_pickups": 5,
  "total_cart": 4,
  "avg_dwell_ms": 6100
}
```

---

## Server Setup

```bash
# Install dependencies
pip install flask flask-cors pyserial

# ESP32-CAM only (REST receiver)
python3 shelf_server.py

# Arduino Uno + REST (serial bridge + REST)
python3 shelf_server.py --port /dev/ttyUSB0        # Linux/Mac
python3 shelf_server.py --port COM3                 # Windows
```

### REST API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| POST | `/api/shelf-event` | Receive ESP32-CAM session payload |
| GET | `/api/metrics` | Current cumulative metrics |
| GET | `/api/events/latest?n=20` | Last N events (feed) |
| GET | `/api/sessions?page=1&limit=50` | Paginated session log |
| POST | `/api/reset` | Reset all metrics (dev/test) |

---

## Dashboard Setup

The dashboard (`shelf_dashboard.jsx`) is a self-contained React component.

**Quick start with Vite:**
```bash
npm create vite@latest shelf-ui -- --template react
cd shelf-ui
cp shelf_dashboard.jsx src/App.jsx
npm install
npm run dev
```

**Connect to live server:**  
Uncomment the polling block in `shelf_dashboard.jsx` (search for `// useEffect`) and replace `YOUR_SERVER` with your server IP.

---

## Deployment Recommendations

| Concern | Recommendation |
|---|---|
| Server host | Raspberry Pi 4 on local LAN, or any Linux box |
| Persistence | Replace in-memory store with SQLite or InfluxDB for long-term data |
| Multi-shelf | Deploy one device per shelf; assign unique `DEVICE_ID` in firmware |
| Dashboard hosting | Serve built React app via Nginx on the same Pi |
| Power | ESP32-CAM draws ~300mA at peak — use a 5V/2A supply |
| Camera angle | Mount 60–90 cm above shelf edge, angled 30° downward |

---

## Tunable Constants (both firmware files)

| Constant | Default | Description |
|---|---|---|
| `SHELF_DISTANCE_CM` | 80 | Max distance to count as customer present |
| `INTEREST_THRESHOLD_MS` | 5000 | Dwell time to mark as "interested" |
| `SESSION_TIMEOUT_MS` | 6000–8000 | No motion grace period before session ends |
| `CART_CONFIRM_MS` | 3000 | Window after pickup to confirm cart add |
| `MOTION_BLOCK_THRESHOLD` | 50 | ESP32-CAM: min changed 8×8 blocks = motion |

---

## Known Limitations

- **Arduino Uno** has no WiFi; requires USB-serial cable or serial-to-WiFi bridge (e.g. ESP8266 as transparent UART bridge)
- **ESP32-CAM** has limited GPIO — only GPIO12 and GPIO13 are safely usable alongside camera
- **Cart detection** is inferred (customer leaves quickly after pickup), not directly observed — accuracy ~80% in testing
- **Multi-person** scenarios (two customers at once) are not handled; the state machine assumes single occupancy per shelf zone

---

## Extension Points

- **MQTT:** Replace `HTTPClient` POST in ESP32-CAM with PubSubClient for broker-based architecture
- **SD card logging:** Add `SD_MMC` library to ESP32-CAM for local session log when WiFi is unavailable
- **Computer vision:** Swap frame-diff with a TFLite person-detection model (e.g. MobileNet SSD) for higher accuracy — requires ESP32-S3 with PSRAM
- **SQLite persistence:** `pip install flask-sqlalchemy` and add a `Session` model to `shelf_server.py`
