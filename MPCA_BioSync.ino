/*
 * ============================================================
 *  MPCA PROJECT — BioSync Smart Pillow
 *  Board  : ESP32 DevKit V1  (Arduino Core 3.x)
 *  Author : PES2UG24CS013 & Team
 * ============================================================
 *  PIN MAP
 *  FSR Left         → GPIO 35  (Analog)
 *  FSR Middle       → GPIO 32  (Analog)
 *  FSR Right        → GPIO 34  (Analog)
 *  DHT22            → GPIO 27  (Digital)
 *  Vibration Motor  → GPIO 25  (PWM)
 *  Touch Sensor     → GPIO 14  (Digital)
 * ============================================================
 *  LIBRARY NEEDED:
 *    DHT sensor library — Adafruit  (+ Adafruit Unified Sensor)
 * ============================================================
 */

#include <Wire.h>
#include <DHT.h>

// ─── PIN DEFINITIONS ─────────────────────────────────────────
#define FSR_L_PIN     35
#define FSR_M_PIN     32
#define FSR_R_PIN     34
#define DHT_PIN       27
#define VIB_PIN       25
#define TOUCH_PIN     14
#define DHT_TYPE      DHT22

// ─── THRESHOLDS & TIMING ─────────────────────────────────────
#define FSR_PRESSURE_THRESHOLD   500
#define FSR_POSTURE_THRESHOLD    800
#define FSR_MID_MIN              300
#define BAD_POSTURE_LIMIT_MS   5000UL
#define SNOOZE_DURATION_MS    10000UL
#define ALARM_DELAY_MS        15000UL
#define TAP_WINDOW_MS           600UL

// ─── PWM (ESP32 Arduino Core 3.x) ────────────────────────────
#define VIB_PWM_FREQ       1000
#define VIB_PWM_RESOLUTION    8     // 0-255

// ─── OBJECTS ─────────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);

// ─── STATE MACHINE ───────────────────────────────────────────
enum SystemState { HIBERNATE, MONITORING, VIBRATING, SNOOZED };
SystemState currentState = HIBERNATE;

// ─── GLOBALS ─────────────────────────────────────────────────
unsigned long badPostureStart = 0;
unsigned long snoozeStart     = 0;
unsigned long alarmScheduled  = 0;
unsigned long lastPrint       = 0;
unsigned long wakeTime        = 0;
int           vibIntensity    = 0;
bool          alarmActive     = false;
int           tapCount        = 0;
unsigned long lastTapTime     = 0;
bool          touchWasHigh    = false;


// ─────────────────────────────────────────────────────────────
//  setVibration — set motor PWM (0 to 255)
// ─────────────────────────────────────────────────────────────
void setVibration(int val) {
  val = constrain(val, 0, 255);
  ledcWrite(VIB_PIN, val);
}


// ─────────────────────────────────────────────────────────────
//  evaluatePosture — true = bad posture
// ─────────────────────────────────────────────────────────────
bool evaluatePosture(int l, int m, int r) {
  if (l > FSR_POSTURE_THRESHOLD && m < FSR_MID_MIN) return true;
  if (r > FSR_POSTURE_THRESHOLD && m < FSR_MID_MIN) return true;
  return false;
}


// ─────────────────────────────────────────────────────────────
//  handleTouch — 1 tap=snooze, 2=dismiss, 3=alarm
// ─────────────────────────────────────────────────────────────
void handleTouch(unsigned long now) {
  bool touchHigh = digitalRead(TOUCH_PIN);

  if (touchHigh && !touchWasHigh) {
    tapCount++;
    lastTapTime  = now;
    touchWasHigh = true;
  }
  if (!touchHigh) touchWasHigh = false;

  if (tapCount > 0 && (now - lastTapTime) > TAP_WINDOW_MS) {
    switch (tapCount) {
      case 1:
        setVibration(0);
        snoozeStart  = now;
        currentState = SNOOZED;
        Serial.println("[TOUCH]   1 tap - Snoozed 10s.");
        break;
      case 2:
        setVibration(0);
        alarmActive  = false;
        vibIntensity = 0;
        currentState = MONITORING;
        Serial.println("[TOUCH]   2 taps - Dismissed.");
        break;
      case 3:
        alarmScheduled = now + ALARM_DELAY_MS;
        Serial.println("[TOUCH]   3 taps - Alarm set +15s.");
        break;
      default:
        Serial.printf("[TOUCH]   %d taps - no action.\n", tapCount);
        break;
    }
    tapCount = 0;
  }
}


// ─────────────────────────────────────────────────────────────
//  printStatus — serial output every 2s
// ─────────────────────────────────────────────────────────────
void printStatus(int l, int m, int r) {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  const char* stateStr[] = { "HIBERNATE", "MONITORING", "VIBRATING", "SNOOZED" };

  Serial.print("[STATUS]  ");
  Serial.printf("L:%-4d M:%-4d R:%-4d", l, m, r);

  if (!isnan(temp)) {
    Serial.printf(" | %.1fC  %.0f%%", temp, hum);
  } else {
    Serial.print(" | DHT:--");
  }

  Serial.printf(" | %s\n", stateStr[currentState]);
}


// ─────────────────────────────────────────────────────────────
//  logSessionSummary — printed when head is removed
// ─────────────────────────────────────────────────────────────
void logSessionSummary(unsigned long now) {
  unsigned long secs = (now - wakeTime) / 1000;
  float temp = dht.readTemperature();

  Serial.println("\n============================================");
  Serial.println("             SESSION SUMMARY");
  Serial.println("============================================");
  Serial.printf("  Duration    : %lu min %lu sec\n", secs / 60, secs % 60);
  if (!isnan(temp)) {
    Serial.printf("  Temperature : %.1fC  (%s)\n",
      temp, temp > 30.0 ? "Warm" : "Comfortable");
  }
  Serial.println("============================================\n");
}


// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TOUCH_PIN, INPUT);
  ledcAttach(VIB_PIN, VIB_PWM_FREQ, VIB_PWM_RESOLUTION);
  setVibration(0);

  dht.begin();

  Serial.println("\n============================================");
  Serial.println("   BioSync Smart Pillow - MPCA Project");
  Serial.println("============================================");
  Serial.println("[SYSTEM]  Ready. Waiting for head pressure...");
}


// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  int  fsrL = analogRead(FSR_L_PIN);
  int  fsrM = analogRead(FSR_M_PIN);
  int  fsrR = analogRead(FSR_R_PIN);
  bool headDetected = (fsrL > FSR_PRESSURE_THRESHOLD ||
                       fsrM > FSR_PRESSURE_THRESHOLD ||
                       fsrR > FSR_PRESSURE_THRESHOLD);

  handleTouch(now);

  switch (currentState) {

    case HIBERNATE:
      if (headDetected) {
        wakeTime     = now;
        currentState = MONITORING;
        Serial.println("[WAKE]    Head detected - monitoring started.");
      }
      break;

    case MONITORING:
      if (!headDetected) {
        logSessionSummary(now);
        setVibration(0);
        badPostureStart = 0;
        currentState    = HIBERNATE;
        break;
      }
      if (evaluatePosture(fsrL, fsrM, fsrR)) {
        if (badPostureStart == 0) {
          badPostureStart = now;
          Serial.println("[POSTURE] Bad posture - timer started.");
        }
        if (now - badPostureStart >= BAD_POSTURE_LIMIT_MS) {
          Serial.println("[POSTURE] Limit reached - vibrating!");
          vibIntensity = 100;
          setVibration(vibIntensity);
          currentState = VIBRATING;
        }
      } else {
        if (badPostureStart != 0) Serial.println("[POSTURE] Corrected.");
        badPostureStart = 0;
        setVibration(0);
      }
      if (alarmScheduled > 0 && now >= alarmScheduled) {
        Serial.println("[ALARM]   Firing!");
        alarmActive    = true;
        vibIntensity   = 200;
        setVibration(vibIntensity);
        alarmScheduled = 0;
        currentState   = VIBRATING;
      }
      break;

    case VIBRATING:
      if (!evaluatePosture(fsrL, fsrM, fsrR) && !alarmActive) {
        Serial.println("[VIBRATE] Posture fixed - off.");
        setVibration(0);
        vibIntensity    = 0;
        badPostureStart = 0;
        currentState    = MONITORING;
      }
      break;

    case SNOOZED:
      if (now - snoozeStart >= SNOOZE_DURATION_MS) {
        Serial.println("[SNOOZE] Finished. System re-armed.");
        badPostureStart = 0;
        alarmActive = false;
        
        currentState    = MONITORING;
      }
      break;
  }

  if (now - lastPrint >= 2000) {
    lastPrint = now;
    printStatus(fsrL, fsrM, fsrR);
  }

  delay(100);
}
