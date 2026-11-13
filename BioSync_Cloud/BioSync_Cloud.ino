/*
 * BioSync Smart Pillow: posture and sleep-environment logger
 * Board: ESP32 DevKit V1 / compatible ESP32 board, Arduino core 3.x
 * Sensors: three FSRs, DHT22, touch input; output: vibration motor driver
 *
 * CSV telemetry is sent over USB serial at 115200 baud for the companion logger.
 * This educational prototype is not a medical device or sleep-quality diagnostic.
 */

#define TS_ENABLE_SSL
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <DHT.h>
#include "ThingSpeak.h"
#include "ThingSpeakTrust.h"
#include "secrets.h"

#include <string.h>

// Existing sensor wiring is preserved; motor control uses GPIO 25.
constexpr uint8_t FSR_LEFT_PIN   = 35;
constexpr uint8_t FSR_MIDDLE_PIN = 32;
constexpr uint8_t FSR_RIGHT_PIN  = 34;
constexpr uint8_t DHT_PIN        = 27;
constexpr uint8_t VIBRATION_PIN  = 25;
constexpr uint8_t TOUCH_PIN      = 14;
constexpr uint8_t DHT_TYPE       = DHT22;

// Calibrate these values from raw readings on the installed pillow.
constexpr int PRESSURE_THRESHOLD = 500;
constexpr int CENTER_THRESHOLD   = 800;
constexpr int SIDE_THRESHOLD     = 500;

constexpr uint32_t SAMPLE_INTERVAL_MS = 2000;
constexpr uint32_t NO_PRESSURE_GRACE_MS = 3000;
constexpr uint32_t BAD_POSTURE_LIMIT_MS = 5000;
constexpr uint32_t TAP_WINDOW_MS = 600;
constexpr uint32_t SNOOZE_DURATION_MS = 10000;
constexpr uint32_t VIBRATION_ON_MS = 250;
constexpr uint32_t VIBRATION_OFF_MS = 750;
constexpr uint8_t VIBRATION_DUTY = 160;
// ThingSpeak's free license accepts one channel update every 15 seconds.
constexpr uint32_t CLOUD_UPDATE_INTERVAL_MS = 16000;

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClientSecure cloudClient;

struct CloudSnapshot {
  int left;
  int middle;
  int right;
  float temperature;
  float humidity;
  int postureCode;
  int alertCode;
  uint32_t activeSessionId;
  uint32_t vibrationOnMs;
  bool environmentValid;
  char posture[24];
  char systemState[16];
  char alert[16];
  char event[24];
};

CloudSnapshot cloudSnapshot = {};
portMUX_TYPE cloudSnapshotMux = portMUX_INITIALIZER_UNLOCKED;
volatile int32_t cloudStatus = -999;
volatile int32_t cloudWifiStatus = -1;
int32_t lastReportedCloudStatus = -999;
int32_t lastReportedCloudWifiStatus = -1;
uint32_t lastWifiAttemptAt = 0;
uint32_t lastCloudUploadAt = 0;

enum SystemState : uint8_t { WAITING, MONITORING };
SystemState state = WAITING;

uint32_t sessionId = 0;
uint32_t sessionStartedAt = 0;
uint32_t lastSampleAt = 0;
uint32_t noPressureStartedAt = 0;
uint32_t centerPostureStartedAt = 0;
uint32_t snoozeUntil = 0;
uint32_t vibrationPhaseUntil = 0;
uint32_t lastVibrationUpdateAt = 0;
uint32_t vibrationOnMs = 0;
uint32_t lastTouchAt = 0;
uint32_t lastTapAt = 0;
uint32_t postureChanges = 0;
uint32_t centerSamples = 0;
uint32_t postureSamples = 0;
uint32_t validEnvironmentSamples = 0;
uint32_t alertCount = 0;
float temperatureSum = 0.0f;
float humiditySum = 0.0f;
uint8_t tapCount = 0;
uint8_t vibrationDuty = 0;
bool previousTouch = false;
bool alertActive = false;
bool alertDismissed = false;
String lastEvent = "BOOT";
String previousPosture = "UNKNOWN";

const char* stateName() { return state == WAITING ? "WAITING" : "MONITORING"; }
const char* alertName(uint32_t now);

bool pressurePresent(int left, int middle, int right) {
  return left >= PRESSURE_THRESHOLD || middle >= PRESSURE_THRESHOLD ||
         right >= PRESSURE_THRESHOLD;
}

String classifyPosture(int left, int middle, int right) {
  if (!pressurePresent(left, middle, right)) return "NO_PRESSURE";
  if (middle >= CENTER_THRESHOLD && middle >= left && middle >= right) return "CENTER_DOMINANT";
  if (max(left, right) >= SIDE_THRESHOLD && max(left, right) > middle) {
    return left > right ? "LEFT_DOMINANT" : "RIGHT_DOMINANT";
  }
  return "DISTRIBUTED";
}

int postureCode(const String& posture) {
  if (posture == "LEFT_DOMINANT") return 1;
  if (posture == "CENTER_DOMINANT") return 2;
  if (posture == "RIGHT_DOMINANT") return 3;
  if (posture == "DISTRIBUTED") return 4;
  return 0;
}

int alertCode(uint32_t now) {
  if (alertDismissed) return 3;
  if (alertActive && snoozeUntil != 0 && now < snoozeUntil) return 2;
  if (alertActive) return 1;
  return 0;
}

void saveCloudSnapshot(uint32_t now, int left, int middle, int right,
                       float temperature, float humidity, bool environmentValid,
                       const String& posture) {
  const uint32_t activeSessionId = state == WAITING ? 0 : sessionId;
  const int activeAlertCode = alertCode(now);
  portENTER_CRITICAL(&cloudSnapshotMux);
  cloudSnapshot.left = left;
  cloudSnapshot.middle = middle;
  cloudSnapshot.right = right;
  cloudSnapshot.temperature = temperature;
  cloudSnapshot.humidity = humidity;
  cloudSnapshot.postureCode = postureCode(posture);
  cloudSnapshot.alertCode = activeAlertCode;
  cloudSnapshot.activeSessionId = activeSessionId;
  cloudSnapshot.vibrationOnMs = vibrationOnMs;
  cloudSnapshot.environmentValid = environmentValid;
  strncpy(cloudSnapshot.posture, posture.c_str(), sizeof(cloudSnapshot.posture) - 1);
  strncpy(cloudSnapshot.systemState, stateName(), sizeof(cloudSnapshot.systemState) - 1);
  strncpy(cloudSnapshot.alert, alertName(now), sizeof(cloudSnapshot.alert) - 1);
  strncpy(cloudSnapshot.event, lastEvent.c_str(), sizeof(cloudSnapshot.event) - 1);
  cloudSnapshot.posture[sizeof(cloudSnapshot.posture) - 1] = '\0';
  cloudSnapshot.systemState[sizeof(cloudSnapshot.systemState) - 1] = '\0';
  cloudSnapshot.alert[sizeof(cloudSnapshot.alert) - 1] = '\0';
  cloudSnapshot.event[sizeof(cloudSnapshot.event) - 1] = '\0';
  portEXIT_CRITICAL(&cloudSnapshotMux);
}

void cloudTask(void* parameter) {
  (void)parameter;
  cloudClient.setCACert(THING_SPEAK_CA_CERT);
  ThingSpeak.begin(cloudClient);

  for (;;) {
    const uint32_t now = millis();
    cloudWifiStatus = WiFi.status() == WL_CONNECTED ? 1 : 0;
    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWifiAttemptAt >= 10000 || lastWifiAttemptAt == 0) {
        lastWifiAttemptAt = now;
        WiFi.mode(WIFI_STA);
        WiFi.begin(SECRET_SSID, SECRET_PASS);
      }
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    if (now - lastCloudUploadAt >= CLOUD_UPDATE_INTERVAL_MS) {
      lastCloudUploadAt = now;
      CloudSnapshot sample;
      portENTER_CRITICAL(&cloudSnapshotMux);
      memcpy(&sample, &cloudSnapshot, sizeof(sample));
      portEXIT_CRITICAL(&cloudSnapshotMux);

      ThingSpeak.setField(1, sample.left);
      ThingSpeak.setField(2, sample.middle);
      ThingSpeak.setField(3, sample.right);
      if (sample.environmentValid) {
        ThingSpeak.setField(4, sample.temperature);
        ThingSpeak.setField(5, sample.humidity);
      }
      ThingSpeak.setField(6, sample.postureCode);
      ThingSpeak.setField(7, sample.alertCode);
      ThingSpeak.setField(8, static_cast<float>(sample.vibrationOnMs) / 1000.0f);

      String status = "session=" + String(sample.activeSessionId) +
                      ";state=" + sample.systemState +
                      ";posture=" + sample.posture +
                      ";alert=" + sample.alert +
                      ";event=" + sample.event;
      ThingSpeak.setStatus(status);
      cloudStatus = static_cast<int32_t>(ThingSpeak.writeFields(SECRET_TS_CHANNEL_ID,
                                                                 SECRET_TS_WRITE_API_KEY));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void accountVibration() {
  const uint32_t now = millis();
  if (vibrationDuty > 0) vibrationOnMs += now - lastVibrationUpdateAt;
  lastVibrationUpdateAt = now;
}

void setVibration(uint8_t duty) {
  accountVibration();
  vibrationDuty = duty;
  ledcWrite(VIBRATION_PIN, duty);
}

void resetSessionStats() {
  postureChanges = centerSamples = postureSamples = validEnvironmentSamples = alertCount = 0;
  temperatureSum = humiditySum = 0.0f;
  centerPostureStartedAt = snoozeUntil = vibrationPhaseUntil = 0;
  alertActive = alertDismissed = false;
  previousPosture = "UNKNOWN";
  vibrationOnMs = 0;
  setVibration(0);
}

void startSession(uint32_t now) {
  ++sessionId;
  sessionStartedAt = now;
  lastSampleAt = now - SAMPLE_INTERVAL_MS;
  noPressureStartedAt = 0;
  resetSessionStats();
  state = MONITORING;
  lastEvent = "SESSION_START";
  Serial.printf("EVENT,%lu,%s\n", static_cast<unsigned long>(sessionId), lastEvent.c_str());
}

void finishSession(uint32_t now) {
  accountVibration();
  const uint32_t seconds = (now - sessionStartedAt) / 1000;
  const float centerPercent = postureSamples ? 100.0f * centerSamples / postureSamples : 0.0f;
  Serial.printf("SUMMARY,%lu,%lu,%lu,%.1f,%.1f,%.1f,%lu,%lu\n",
                static_cast<unsigned long>(sessionId), static_cast<unsigned long>(seconds),
                static_cast<unsigned long>(postureChanges), centerPercent,
                validEnvironmentSamples ? temperatureSum / validEnvironmentSamples : NAN,
                validEnvironmentSamples ? humiditySum / validEnvironmentSamples : NAN,
                static_cast<unsigned long>(alertCount),
                static_cast<unsigned long>(vibrationOnMs));
  setVibration(0);
  lastEvent = "SESSION_END";
  Serial.printf("EVENT,%lu,%s\n", static_cast<unsigned long>(sessionId), lastEvent.c_str());
  state = WAITING;
  noPressureStartedAt = 0;
  resetSessionStats();
}

void handleTouch(uint32_t now) {
  const bool touchNow = digitalRead(TOUCH_PIN) == HIGH;
  if (touchNow && !previousTouch && now - lastTouchAt >= 250) {
    lastTouchAt = now;
    if (tapCount == 0 || now - lastTapAt > TAP_WINDOW_MS) tapCount = 1;
    else ++tapCount;
    lastTapAt = now;
  }
  previousTouch = touchNow;

  if (tapCount > 0 && now - lastTapAt > TAP_WINDOW_MS) {
    if (tapCount == 1) {
      if (alertActive && !alertDismissed) {
        snoozeUntil = now + SNOOZE_DURATION_MS;
        lastEvent = "SNOOZE";
      } else lastEvent = "USER_MARK";
    } else if (tapCount == 2 && alertActive) {
      alertActive = false;
      alertDismissed = true;
      setVibration(0);
      lastEvent = "ALERT_DISMISSED";
    } else {
      lastEvent = "USER_MARK";
    }
    const uint32_t eventSessionId = state == WAITING ? 0 : sessionId;
    Serial.printf("EVENT,%lu,%s\n", static_cast<unsigned long>(eventSessionId), lastEvent.c_str());
    tapCount = 0;
  }
}

void updatePostureAlert(uint32_t now, const String& posture) {
  if (posture != "CENTER_DOMINANT") {
    centerPostureStartedAt = 0;
    snoozeUntil = 0;
    alertActive = false;
    alertDismissed = false;
    setVibration(0);
    return;
  }

  if (centerPostureStartedAt == 0) centerPostureStartedAt = now;
  if (!alertActive && !alertDismissed && now - centerPostureStartedAt >= BAD_POSTURE_LIMIT_MS) {
    alertActive = true;
    ++alertCount;
    lastEvent = "ALERT_START";
    Serial.printf("EVENT,%lu,%s\n", static_cast<unsigned long>(sessionId), lastEvent.c_str());
  }

  if (!alertActive || alertDismissed || (snoozeUntil != 0 && now < snoozeUntil)) {
    setVibration(0);
    return;
  }

  if (now >= vibrationPhaseUntil) {
    if (vibrationDuty == 0) {
      setVibration(VIBRATION_DUTY);
      vibrationPhaseUntil = now + VIBRATION_ON_MS;
    } else {
      setVibration(0);
      vibrationPhaseUntil = now + VIBRATION_OFF_MS;
    }
  }
}

const char* alertName(uint32_t now) {
  if (alertDismissed) return "DISMISSED";
  if (alertActive && snoozeUntil != 0 && now < snoozeUntil) return "SNOOZED";
  if (alertActive) return "ACTIVE";
  return "OFF";
}

void emitSample(uint32_t now, int left, int middle, int right) {
  accountVibration();
  const String posture = classifyPosture(left, middle, right);
  const float temperature = dht.readTemperature();
  const float humidity = dht.readHumidity();
  const bool validEnvironment = !isnan(temperature) && !isnan(humidity);

  if (posture != previousPosture && previousPosture != "UNKNOWN") {
    ++postureChanges;
    lastEvent = "POSTURE_CHANGE";
  }
  previousPosture = posture;
  if (posture != "NO_PRESSURE") {
    ++postureSamples;
    if (posture == "CENTER_DOMINANT") ++centerSamples;
  }
  if (validEnvironment) {
    temperatureSum += temperature;
    humiditySum += humidity;
    ++validEnvironmentSamples;
  }
  saveCloudSnapshot(now, left, middle, right, temperature, humidity, validEnvironment, posture);

  // DATA,uptime_ms,session_id,fsr_left,fsr_middle,fsr_right,temp_c,humidity_pct,
  // posture,state,vibration_pwm,vibration_on_ms,alert,event
  const uint32_t dataSessionId = state == WAITING ? 0 : sessionId;
  Serial.printf("DATA,%lu,%lu,%d,%d,%d,", static_cast<unsigned long>(now),
                static_cast<unsigned long>(dataSessionId), left, middle, right);
  if (validEnvironment) Serial.printf("%.1f,%.1f", temperature, humidity);
  else Serial.print(",");
  Serial.printf(",%s,%s,%u,%lu,%s,%s\n", posture.c_str(), stateName(), vibrationDuty,
                static_cast<unsigned long>(vibrationOnMs),
                alertName(now), lastEvent.c_str());
  lastEvent = "NONE";
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(TOUCH_PIN, INPUT);
  analogReadResolution(12);
  dht.begin();
  lastVibrationUpdateAt = millis();
  if (!ledcAttach(VIBRATION_PIN, 1000, 8)) {
    Serial.println("ERROR: PWM setup failed for GPIO 25; check ESP32 board/core selection.");
  }
  setVibration(0);

  if (xTaskCreatePinnedToCore(cloudTask, "thingSpeakUpload", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("ERROR: could not start cloud upload task.");
  }

  Serial.println("BioSync Smart Pillow - posture and environment logger");
  Serial.println("DATA_HEADER,uptime_ms,session_id,fsr_left,fsr_middle,fsr_right,temp_c,humidity_pct,posture,state,vibration_pwm,vibration_on_ms,alert,event");
  Serial.println("SUMMARY_HEADER,session_id,duration_s,posture_changes,center_dominant_pct,mean_temp_c,mean_humidity_pct,alert_count,vibration_on_ms");
  Serial.println("Ready; waiting for pressure.");
  Serial.println("Cloud updates are sent securely to ThingSpeak every 16 seconds when Wi-Fi is available.");
}

void loop() {
  const uint32_t now = millis();
  const int left = analogRead(FSR_LEFT_PIN);
  const int middle = analogRead(FSR_MIDDLE_PIN);
  const int right = analogRead(FSR_RIGHT_PIN);
  const bool detected = pressurePresent(left, middle, right);

  handleTouch(now);

  if (state == WAITING && detected) {
    startSession(now);
  } else if (state == MONITORING) {
    if (!detected) {
      if (noPressureStartedAt == 0) noPressureStartedAt = now;
      else if (now - noPressureStartedAt >= NO_PRESSURE_GRACE_MS) finishSession(now);
      centerPostureStartedAt = 0;
      alertActive = false;
      alertDismissed = false;
      snoozeUntil = 0;
      setVibration(0);
    } else {
      noPressureStartedAt = 0;
      const String posture = classifyPosture(left, middle, right);
      updatePostureAlert(now, posture);
    }
  }

  if (now - lastSampleAt >= SAMPLE_INTERVAL_MS) {
    lastSampleAt = now;
    emitSample(now, left, middle, right);
  }
  if (cloudWifiStatus != lastReportedCloudWifiStatus) {
    if (cloudWifiStatus == 1) Serial.println("[CLOUD] Wi-Fi connected.");
    else if (cloudWifiStatus == 0) Serial.println("[CLOUD] Wi-Fi not connected; check SSID, password, and 2.4 GHz network.");
    lastReportedCloudWifiStatus = cloudWifiStatus;
  }
  if (cloudStatus != -999 && cloudStatus != lastReportedCloudStatus) {
    Serial.printf("[CLOUD] ThingSpeak response code: %ld\n", static_cast<long>(cloudStatus));
    lastReportedCloudStatus = cloudStatus;
  }
  delay(50);
}
