/*
 * ============================================================
 *  SHELF ANALYTICS SYSTEM — Arduino Uno
 *  Hardware: PIR sensor + HC-SR04 Ultrasonic sensor
 *  Output  : Serial (115200 baud) JSON metrics
 * ============================================================
 *
 *  WIRING:
 *  ┌──────────────────────────────────────┐
 *  │ PIR Sensor (HC-SR501)               │
 *  │   VCC  → 5V                         │
 *  │   GND  → GND                        │
 *  │   OUT  → D2                         │
 *  │                                     │
 *  │ Ultrasonic (HC-SR04)                │
 *  │   VCC  → 5V                         │
 *  │   GND  → GND                        │
 *  │   TRIG → D7                         │
 *  │   ECHO → D8                         │
 *  │                                     │
 *  │ Optional: Vibration sensor          │
 *  │   (detects item pick-up from shelf) │
 *  │   OUT  → D3                         │
 *  └──────────────────────────────────────┘
 *
 *  DETECTION LOGIC:
 *  - PIR detects human presence
 *  - Ultrasonic confirms distance (customer near shelf < SHELF_DISTANCE_CM)
 *  - Vibration sensor detects shelf disturbance (pick-up event)
 *  - "Add to cart" inferred if customer leaves after pick-up
 */

// ─── Pin Definitions ───────────────────────────────────────
#define PIR_PIN          2    // PIR motion sensor output
#define TRIG_PIN         7    // Ultrasonic trigger
#define ECHO_PIN         8    // Ultrasonic echo
#define VIBRATION_PIN    3    // Vibration/shock sensor (optional)
#define LED_ACTIVITY     13   // Built-in LED: blinks on events

// ─── Tunable Thresholds ────────────────────────────────────
#define SHELF_DISTANCE_CM     80    // Max cm to count as "near shelf"
#define INTEREST_THRESHOLD_MS 5000  // 5s dwell = interested customer
#define CART_CONFIRM_MS       3000  // If leaves within 3s after pickup = added to cart
#define DEBOUNCE_MS           200   // PIR debounce
#define SESSION_TIMEOUT_MS    8000  // No motion for 8s = session ended
#define ULTRASONIC_INTERVAL   300   // Measure distance every 300ms
#define REPORT_INTERVAL_MS    10000 // Print full report every 10s

// ─── State Machine ─────────────────────────────────────────
enum ShelfState {
  STATE_IDLE,         // No customer nearby
  STATE_DETECTED,     // Customer detected, timing started
  STATE_INTERESTED,   // Dwell > 5s
  STATE_PICKUP,       // Item picked up (vibration triggered)
  STATE_LEAVING       // Customer moving away, confirm cart action
};

ShelfState currentState = STATE_IDLE;

// ─── Session Data ──────────────────────────────────────────
struct Session {
  unsigned long startTime;
  unsigned long endTime;
  unsigned long dwellTime;
  bool          pickedUp;
  bool          addedToCart;
  bool          interested;    // dwellTime > INTEREST_THRESHOLD
};

// ─── Cumulative Metrics ────────────────────────────────────
struct Metrics {
  uint16_t footfall;          // Total unique customer detections
  uint16_t interestedCount;   // Customers who stayed > 5s
  uint16_t pickupCount;       // Items picked from shelf
  uint16_t addToCartCount;    // Items actually added to cart
  unsigned long totalDwellMs; // Sum of all dwell times
  uint16_t sessionCount;      // Completed sessions
} metrics = {0, 0, 0, 0, 0, 0};

// ─── Runtime Variables ─────────────────────────────────────
Session      activeSession;
bool         pirState         = false;
bool         lastPirState     = false;
bool         vibrationTriggered = false;
unsigned long lastMotionTime  = 0;
unsigned long lastUltrasonicTime = 0;
unsigned long lastReportTime  = 0;
unsigned long lastDebounceTime = 0;
float        currentDistance  = 999.0;

// ───────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  pinMode(LED_ACTIVITY, OUTPUT);

  // Let PIR sensor warm up (30s for HC-SR501, skip in testing)
  Serial.println(F("{\"event\":\"boot\",\"status\":\"PIR warmup 2s\"}"));
  delay(2000);

  Serial.println(F("{\"event\":\"ready\",\"device\":\"Arduino-Uno-ShelfAnalytics\",\"version\":\"1.0\"}"));
}

// ───────────────────────────────────────────────────────────
float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 25000); // 25ms timeout (~430cm max)
  if (duration == 0) return 999.0; // Out of range
  return (duration * 0.034) / 2.0;
}

bool isCustomerNear() {
  return (currentDistance > 0 && currentDistance < SHELF_DISTANCE_CM);
}

void startSession() {
  activeSession.startTime   = millis();
  activeSession.endTime     = 0;
  activeSession.dwellTime   = 0;
  activeSession.pickedUp    = false;
  activeSession.addedToCart = false;
  activeSession.interested  = false;
  metrics.footfall++;
  blinkLED(1);
  Serial.print(F("{\"event\":\"customer_detected\",\"footfall\":"));
  Serial.print(metrics.footfall);
  Serial.print(F(",\"distance_cm\":"));
  Serial.print(currentDistance, 1);
  Serial.println(F("}"));
}

void endSession() {
  activeSession.endTime   = millis();
  activeSession.dwellTime = activeSession.endTime - activeSession.startTime;
  activeSession.interested = (activeSession.dwellTime >= INTEREST_THRESHOLD_MS);

  if (activeSession.interested)  metrics.interestedCount++;
  if (activeSession.pickedUp)    metrics.pickupCount++;
  if (activeSession.addedToCart) metrics.addToCartCount++;
  metrics.totalDwellMs += activeSession.dwellTime;
  metrics.sessionCount++;

  blinkLED(2);

  // Emit session JSON
  Serial.print(F("{\"event\":\"session_end\",\"dwell_ms\":"));
  Serial.print(activeSession.dwellTime);
  Serial.print(F(",\"interested\":"));
  Serial.print(activeSession.interested ? F("true") : F("false"));
  Serial.print(F(",\"picked_up\":"));
  Serial.print(activeSession.pickedUp ? F("true") : F("false"));
  Serial.print(F(",\"added_to_cart\":"));
  Serial.print(activeSession.addedToCart ? F("true") : F("false"));
  Serial.println(F("}"));

  currentState = STATE_IDLE;
}

void printMetrics() {
  unsigned long avgDwell = (metrics.sessionCount > 0)
    ? (metrics.totalDwellMs / metrics.sessionCount) : 0;

  Serial.print(F("{\"event\":\"metrics_report\""));
  Serial.print(F(",\"footfall\":")); Serial.print(metrics.footfall);
  Serial.print(F(",\"interested\":")); Serial.print(metrics.interestedCount);
  Serial.print(F(",\"pickups\":")); Serial.print(metrics.pickupCount);
  Serial.print(F(",\"add_to_cart\":")); Serial.print(metrics.addToCartCount);
  Serial.print(F(",\"avg_dwell_ms\":")); Serial.print(avgDwell);
  Serial.print(F(",\"total_sessions\":")); Serial.print(metrics.sessionCount);
  // Conversion rates
  if (metrics.footfall > 0) {
    Serial.print(F(",\"interest_rate_pct\":"));
    Serial.print((metrics.interestedCount * 100UL) / metrics.footfall);
    Serial.print(F(",\"pickup_rate_pct\":"));
    Serial.print((metrics.pickupCount * 100UL) / metrics.footfall);
    Serial.print(F(",\"cart_rate_pct\":"));
    Serial.print((metrics.addToCartCount * 100UL) / metrics.footfall);
  }
  Serial.println(F("}"));
}

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_ACTIVITY, HIGH);
    delay(80);
    digitalWrite(LED_ACTIVITY, LOW);
    delay(80);
  }
}

// ───────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // ── 1. Read ultrasonic periodically (non-blocking) ──────
  if (now - lastUltrasonicTime > ULTRASONIC_INTERVAL) {
    currentDistance = measureDistance();
    lastUltrasonicTime = now;
  }

  // ── 2. Read PIR with debounce ───────────────────────────
  bool rawPir = digitalRead(PIR_PIN);
  if (rawPir != lastPirState) {
    lastDebounceTime = now;
    lastPirState = rawPir;
  }
  if ((now - lastDebounceTime) > DEBOUNCE_MS) {
    pirState = rawPir;
  }

  // ── 3. Read vibration sensor (active LOW with pullup) ───
  if (digitalRead(VIBRATION_PIN) == LOW && !vibrationTriggered) {
    vibrationTriggered = true;
    if (currentState == STATE_INTERESTED || currentState == STATE_DETECTED) {
      activeSession.pickedUp = true;
      currentState = STATE_PICKUP;
      Serial.println(F("{\"event\":\"item_pickup_detected\"}"));
    }
  }
  if (digitalRead(VIBRATION_PIN) == HIGH) {
    vibrationTriggered = false; // reset for next pick
  }

  // ── 4. State Machine ────────────────────────────────────
  bool nearShelf = pirState && isCustomerNear();

  switch (currentState) {

    case STATE_IDLE:
      if (nearShelf) {
        startSession();
        currentState = STATE_DETECTED;
        lastMotionTime = now;
      }
      break;

    case STATE_DETECTED:
      if (nearShelf) {
        lastMotionTime = now;
        // Check for interest threshold
        if ((now - activeSession.startTime) >= INTEREST_THRESHOLD_MS) {
          currentState = STATE_INTERESTED;
          metrics.interestedCount++;  // will be corrected in endSession, tracked here for live emit
          Serial.print(F("{\"event\":\"customer_interested\",\"dwell_ms\":"));
          Serial.print(now - activeSession.startTime);
          Serial.println(F("}"));
        }
      } else {
        // Customer moved away
        if ((now - lastMotionTime) > SESSION_TIMEOUT_MS) {
          endSession();
        }
      }
      break;

    case STATE_INTERESTED:
      if (nearShelf) {
        lastMotionTime = now;
      } else {
        if ((now - lastMotionTime) > SESSION_TIMEOUT_MS) {
          endSession();
        }
      }
      break;

    case STATE_PICKUP:
      // After pickup, monitor if customer leaves (= added to cart)
      if (!nearShelf) {
        if ((now - lastMotionTime) > CART_CONFIRM_MS) {
          activeSession.addedToCart = true;
          Serial.println(F("{\"event\":\"item_added_to_cart\"}"));
          endSession();
        }
      } else {
        lastMotionTime = now;
        // Customer still near — maybe put it back, keep monitoring
        if ((now - lastMotionTime) > SESSION_TIMEOUT_MS) {
          // Stayed too long after pickup = put back, not added to cart
          endSession();
        }
      }
      break;

    case STATE_LEAVING:
      endSession();
      break;
  }

  // ── 5. Periodic full metrics report ─────────────────────
  if (now - lastReportTime > REPORT_INTERVAL_MS) {
    printMetrics();
    lastReportTime = now;
  }
}

/*
 * ============================================================
 * SERIAL OUTPUT FORMAT (115200 baud):
 * All events are newline-delimited JSON for easy parsing.
 *
 * {"event":"customer_detected","footfall":3,"distance_cm":42.5}
 * {"event":"customer_interested","dwell_ms":5120}
 * {"event":"item_pickup_detected"}
 * {"event":"item_added_to_cart"}
 * {"event":"session_end","dwell_ms":8300,"interested":true,"picked_up":true,"added_to_cart":true}
 * {"event":"metrics_report","footfall":5,"interested":3,"pickups":2,"add_to_cart":1,
 *   "avg_dwell_ms":6200,"total_sessions":5,"interest_rate_pct":60,
 *   "pickup_rate_pct":40,"cart_rate_pct":20}
 * ============================================================
 */
