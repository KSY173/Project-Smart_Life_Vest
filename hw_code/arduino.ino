/*
  Smart Life Vest Unified Firmware
  - Sensors: GPS, ICM-20948 IMU, AD8232 ECG, MAX30102/MAX30105 PPG, TMP102 temperature
  - Actuator: L298N motor driver
  - Communication: BLE notify packet for Flutter main.dart

  BLE packet keys used by Flutter:
  LAT,LON,COMB_BPM,TEMP,PredictedDirection,ACTIVE,WARNING,
  BASE_TEMP,BASE_COMB_BPM,MADEANGLE,PPG_BPM

  Hardware note:
  - GPS pins are separated from AD8232 LO+/LO- pins to avoid pin conflict.
  - Change GPS_RX_PIN/GPS_TX_PIN if your wiring is different.
*/

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include "ICM_20948.h"
#include "MAX30105.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// -----------------------------------------------------------------------------
// Pin and hardware settings
// -----------------------------------------------------------------------------

#define TMP102_ADDR 0x48

#define ECG_PIN       A0
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3

#define ENA_PIN       9
#define IN1_PIN       7
#define IN2_PIN       8

// IMPORTANT: Do not use the same pins as LO_PLUS_PIN/LO_MINUS_PIN.
#define GPS_RX_PIN    16
#define GPS_TX_PIN    17

// -----------------------------------------------------------------------------
// BLE settings
// -----------------------------------------------------------------------------

#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"
#define BLE_DEVICE_NAME     "LifeVest_UNIFIED"

BLEServer* bleServer = nullptr;
BLECharacteristic* bleCharacteristic = nullptr;

// -----------------------------------------------------------------------------
// Timing and thresholds
// -----------------------------------------------------------------------------

const unsigned long GPS_CHECK_INTERVAL_MS = 2000UL;
const uint8_t GPS_LOSS_THRESHOLD = 10;

const unsigned long SENSOR_INTERVAL_MS = 100UL;
const unsigned long BLE_NOTIFY_INTERVAL_MS = 1000UL;

const unsigned long BASELINE_START_MS = 20000UL;
const unsigned long BASELINE_END_MS = 50000UL;
const int MAX_SAMPLES = 600;

const unsigned long DR_DURATION_MS = 60000UL;

const unsigned long WARNING_DELAY_MS = 6000UL;
const unsigned long ACTIVE_DELAY_MS = 10000UL;

// PPG
#define PPG_IR_FINGER_THRESHOLD 15000UL
#define PPG_AC_THRESH 200
#define PPG_MIN_BEAT_INTERVAL_MS 400
#define PPG_BEAT_HISTORY_LEN 6
#define PPG_BEAT_WINDOW_MS 12000
#define PPG_EMA_ALPHA 0.2f
#define PPG_MIN_VALID 30.0f
#define PPG_MAX_VALID 180.0f
#define PPG_JUMP_MAX 80.0f

// ECG
#define ECG_DYNAMIC_THRESHOLD_DEFAULT 10
#define ECG_NO_PEAK_TIMEOUT_MS 8000
#define ECG_MIN_RR_MS 400
#define ECG_MAX_RR_MS 1000
#define ECG_REFRACTORY_MS 350
#define ECG_RR_HISTORY_LEN 4
#define ECG_MIN_VALID_BPM 40
#define ECG_MAX_VALID_BPM 140
#define ECG_MAX_STEP_UP 10
#define ECG_MAX_STEP_DOWN 15

// Combined BPM
const float ECG_WEIGHT = 0.7f;
const float PPG_WEIGHT = 0.3f;

// IMU complementary filter
const float G_UNIT = 1.0f;
const float ALPHA_RP = 0.98f;
const float ALPHA_HEADING = 0.98f;

// -----------------------------------------------------------------------------
// Global state
// -----------------------------------------------------------------------------

TinyGPSPlus gps;
ICM_20948_I2C imu;
MAX30105 ppg;

bool hasGpsFix = false;
bool gpsLost = false;
uint8_t gpsLossCount = 0;
double lastGpsLat = 0.0;
double lastGpsLon = 0.0;
unsigned long lastGpsCheckMs = 0;

bool drActive = false;
unsigned long drStartMs = 0;
unsigned long lastImuUs = 0;

float roll = 0.0f;
float pitch = 0.0f;
float yawDeg = 0.0f;
double sumAxWorld = 0.0;
double sumAyWorld = 0.0;
double sumAzWorld = 0.0;
unsigned long imuSampleCount = 0;

float predictedDirection = 0.0f;
char madeAngle[8] = " ";

int64_t ppgBeatTimes[PPG_BEAT_HISTORY_LEN] = {0};
int ppgBeatIndex = 0;
int ppgBeatCount = 0;
float ppgBpmEma = 0.0f;
uint32_t ppgBaseline = 0;

bool ecgIsPeak = false;
uint64_t ecgLastPeakTime = 0;
uint32_t ecgRrHistory[ECG_RR_HISTORY_LEN] = {0};
int ecgRrCount = 0;
int ecgRrIndex = 0;
int ecgLastValidBpm = 0;
float ecgBaseline = 0.0f;
const float ECG_BASE_ALPHA = 0.02f;
float ecgDynamicThreshold = ECG_DYNAMIC_THRESHOLD_DEFAULT;

float ecgSamples[MAX_SAMPLES] = {0};
float ppgSamples[MAX_SAMPLES] = {0};
float tempSamples[MAX_SAMPLES] = {0};
int sampleIndex = 0;
bool baselineDone = false;
bool baselineStartPrinted = false;

float ecgMean = 0.0f;
float ppgMean = 0.0f;
float tempMean = 0.0f;
float combinedBpmBase = 0.0f;
float combinedThreshold = 40.0f;

float ecgBpm = 0.0f;
float ppgBpm = 0.0f;
float combinedBpm = 0.0f;
float bodyTemp = 0.0f;
float tiltDegEma = 0.0f;

bool warningFlag = false;
bool activeFlag = false;
bool dangerOngoing = false;
unsigned long dangerStartMs = 0;

unsigned long lastSensorReadMs = 0;
unsigned long lastBleNotifyMs = 0;

// -----------------------------------------------------------------------------
// BLE callbacks
// -----------------------------------------------------------------------------

class LifeVestServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(BLEServer* server) override {
    Serial.println("[BLE] Client disconnected. Restart advertising.");
    server->getAdvertising()->start();
  }
};

// -----------------------------------------------------------------------------
// Utility functions
// -----------------------------------------------------------------------------

float safeDivide(float sum, int count, float fallback) {
  return (count > 0) ? (sum / count) : fallback;
}

float normalizeDeg360(float angle) {
  while (angle >= 360.0f) angle -= 360.0f;
  while (angle < 0.0f) angle += 360.0f;
  return angle;
}

const char* directionToText(float degree) {
  degree = normalizeDeg360(degree);

  if (degree < 22.5f || degree >= 337.5f) return "N";
  if (degree < 67.5f) return "NE";
  if (degree < 112.5f) return "E";
  if (degree < 157.5f) return "SE";
  if (degree < 202.5f) return "S";
  if (degree < 247.5f) return "SW";
  if (degree < 292.5f) return "W";
  return "NW";
}

// -----------------------------------------------------------------------------
// Sensor read functions
// -----------------------------------------------------------------------------

float readTemperatureTMP102() {
  Wire.beginTransmission(TMP102_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) {
    return bodyTemp;
  }

  Wire.requestFrom(TMP102_ADDR, 2);
  if (Wire.available() < 2) {
    return bodyTemp;
  }

  int msb = Wire.read();
  int lsb = Wire.read();
  int16_t rawTemp = ((msb << 8) | lsb) >> 4;
  return rawTemp * 0.0625f;
}

bool beginIMU() {
  if (imu.begin(Wire, 1) == ICM_20948_Stat_Ok) return true;
  if (imu.begin(Wire, 0) == ICM_20948_Stat_Ok) return true;
  return false;
}

bool readTiltDeg(float &tiltDeg) {
  if (!imu.dataReady()) return false;
  imu.getAGMT();

  float ax = imu.accX();
  float ay = imu.accY();
  float az = imu.accZ();
  float horizontal = sqrtf(ax * ax + ay * ay);

  tiltDeg = atan2f(horizontal, az) * 180.0f / PI;
  return true;
}

int readEcgRaw() {
  if (digitalRead(LO_PLUS_PIN) == HIGH || digitalRead(LO_MINUS_PIN) == HIGH) {
    return 0;
  }
  return analogRead(ECG_PIN);
}

float processEcgAndGetBpm() {
  int raw = readEcgRaw();
  if (raw <= 0) return 0.0f;

  uint64_t now = millis();

  if (ecgBaseline == 0.0f) ecgBaseline = raw;
  ecgBaseline = ecgBaseline + ECG_BASE_ALPHA * ((float)raw - ecgBaseline);

  int dynamicValue = raw - (int)round(ecgBaseline);

  if ((dynamicValue > ecgDynamicThreshold) && !ecgIsPeak) {
    uint64_t nowPeak = now;

    if (ecgLastPeakTime == 0 || (nowPeak - ecgLastPeakTime) >= ECG_REFRACTORY_MS) {
      ecgIsPeak = true;

      if (ecgLastPeakTime != 0) {
        uint32_t rr = (uint32_t)(nowPeak - ecgLastPeakTime);

        if (rr > ECG_NO_PEAK_TIMEOUT_MS) {
          ecgRrCount = 0;
        } else if (rr >= ECG_MIN_RR_MS && rr <= ECG_MAX_RR_MS) {
          ecgRrHistory[ecgRrIndex] = rr;
          ecgRrIndex = (ecgRrIndex + 1) % ECG_RR_HISTORY_LEN;
          if (ecgRrCount < ECG_RR_HISTORY_LEN) ecgRrCount++;

          uint64_t sum = 0;
          for (int i = 0; i < ecgRrCount; i++) sum += ecgRrHistory[i];

          uint32_t avgRr = (uint32_t)(sum / ecgRrCount);
          int candidateBpm = (int)(60000UL / avgRr);

          if (candidateBpm >= ECG_MIN_VALID_BPM && candidateBpm <= ECG_MAX_VALID_BPM) {
            if (ecgLastValidBpm == 0) {
              ecgLastValidBpm = candidateBpm;
            } else {
              int diff = candidateBpm - ecgLastValidBpm;
              if (diff > ECG_MAX_STEP_UP) diff = ECG_MAX_STEP_UP;
              if (diff < -ECG_MAX_STEP_DOWN) diff = -ECG_MAX_STEP_DOWN;
              ecgLastValidBpm += diff;
            }
          }
        }
      }
      ecgLastPeakTime = nowPeak;
    }
  }

  if (dynamicValue < (ecgDynamicThreshold / 2)) {
    ecgIsPeak = false;
  }

  if (ecgLastPeakTime != 0 && (now - ecgLastPeakTime) <= ECG_NO_PEAK_TIMEOUT_MS) {
    return (float)ecgLastValidBpm;
  }
  return 0.0f;
}

void addPpgBeatTime(int64_t timeMs) {
  ppgBeatTimes[ppgBeatIndex] = timeMs;
  ppgBeatIndex = (ppgBeatIndex + 1) % PPG_BEAT_HISTORY_LEN;
  if (ppgBeatCount < PPG_BEAT_HISTORY_LEN) ppgBeatCount++;
}

float computePpgBpmFromHistory(int64_t nowMs) {
  if (ppgBeatCount < 2) return -1.0f;

  int valid = 0;
  int64_t first = 0;
  int64_t last = 0;
  int idx = (ppgBeatIndex - ppgBeatCount + PPG_BEAT_HISTORY_LEN) % PPG_BEAT_HISTORY_LEN;

  for (int i = 0; i < ppgBeatCount; i++) {
    int64_t beatTime = ppgBeatTimes[idx];
    idx = (idx + 1) % PPG_BEAT_HISTORY_LEN;

    if (nowMs - beatTime <= PPG_BEAT_WINDOW_MS) {
      if (valid == 0) first = beatTime;
      last = beatTime;
      valid++;
    }
  }

  if (valid < 2) return -1.0f;
  float intervalMs = (float)(last - first) / (float)(valid - 1);
  if (intervalMs <= 0.0f) return -1.0f;

  return 60000.0f / intervalMs;
}

float processPpgAndGetBpm() {
  ppg.check();
  long irValue = ppg.getIR();
  uint64_t now = millis();

  if (irValue < (long)PPG_IR_FINGER_THRESHOLD) {
    return 0.0f;
  }

  if (ppgBaseline == 0) {
    ppgBaseline = (uint32_t)irValue;
  } else {
    ppgBaseline = ppgBaseline + ((int32_t)irValue - (int32_t)ppgBaseline) / 32;
  }

  int32_t ac = (int32_t)irValue - (int32_t)ppgBaseline;

  static int32_t lastAc = 0;
  static bool rising = false;
  static uint64_t lastBeatMs = 0;

  if (ac > PPG_AC_THRESH && lastAc <= PPG_AC_THRESH && !rising) {
    if (now - lastBeatMs >= PPG_MIN_BEAT_INTERVAL_MS) {
      rising = true;
      lastBeatMs = now;
      addPpgBeatTime(now);
    }
  }

  if (ac < PPG_AC_THRESH / 2) {
    rising = false;
  }
  lastAc = ac;

  float instantBpm = computePpgBpmFromHistory(now);

  if (instantBpm > 0.0f && instantBpm >= PPG_MIN_VALID && instantBpm <= PPG_MAX_VALID) {
    bool accept = true;
    if (ppgBpmEma > 0.0f) {
      float diff = fabsf(instantBpm - ppgBpmEma);
      if (diff > PPG_JUMP_MAX) accept = false;
    }

    if (accept) {
      if (ppgBpmEma == 0.0f) {
        ppgBpmEma = instantBpm;
      } else {
        ppgBpmEma = PPG_EMA_ALPHA * instantBpm + (1.0f - PPG_EMA_ALPHA) * ppgBpmEma;
      }
    }
  }

  if (ppgBpmEma > 0.0f) return ppgBpmEma;
  if (instantBpm > 0.0f) return instantBpm;
  return 0.0f;
}

float computeCombinedBpm(float ecgValue, float ppgValue) {
  bool ecgValid = ecgValue > 0.0f;
  bool ppgValid = ppgValue > 0.0f;

  if (ecgValid && ppgValid) {
    return ECG_WEIGHT * ecgValue + PPG_WEIGHT * ppgValue;
  }
  if (ecgValid) return ecgValue;
  if (ppgValid) return ppgValue;
  return 0.0f;
}

// -----------------------------------------------------------------------------
// Baseline and decision functions
// -----------------------------------------------------------------------------

void computeBaselineStats() {
  float ecgSum = 0.0f;
  float ppgSum = 0.0f;
  float tempSum = 0.0f;
  int ecgCount = 0;
  int ppgCount = 0;
  int tempCount = 0;

  for (int i = 0; i < sampleIndex; i++) {
    if (ecgSamples[i] > 0.0f) {
      ecgSum += ecgSamples[i];
      ecgCount++;
    }
    if (ppgSamples[i] > 0.0f) {
      ppgSum += ppgSamples[i];
      ppgCount++;
    }
    if (tempSamples[i] > 0.0f) {
      tempSum += tempSamples[i];
      tempCount++;
    }
  }

  ecgMean = safeDivide(ecgSum, ecgCount, 80.0f);
  ppgMean = safeDivide(ppgSum, ppgCount, 75.0f);
  tempMean = safeDivide(tempSum, tempCount, bodyTemp > 0.0f ? bodyTemp : 36.5f);

  float combinedSamples[MAX_SAMPLES];
  int combinedCount = 0;

  for (int i = 0; i < sampleIndex; i++) {
    float value = computeCombinedBpm(ecgSamples[i], ppgSamples[i]);
    if (value > 0.0f) {
      combinedSamples[combinedCount++] = value;
    }
  }

  combinedBpmBase = computeCombinedBpm(ecgMean, ppgMean);

  if (combinedCount > 1) {
    float sum = 0.0f;
    for (int i = 0; i < combinedCount; i++) sum += combinedSamples[i];
    float mean = sum / combinedCount;

    float variance = 0.0f;
    for (int i = 0; i < combinedCount; i++) {
      float diff = combinedSamples[i] - mean;
      variance += diff * diff;
    }

    float stdDev = sqrtf(variance / combinedCount);
    combinedThreshold = max(40.0f, max(mean - 1.5f * stdDev, mean * 0.75f));
  } else {
    combinedThreshold = max(40.0f, combinedBpmBase * 0.75f);
  }
}

void updateBaselineCollection() {
  unsigned long now = millis();

  if (baselineDone) return;

  if (now >= BASELINE_START_MS) {
    if (!baselineStartPrinted) {
      Serial.println("[BASELINE] Collection started");
      baselineStartPrinted = true;
    }

    if (sampleIndex < MAX_SAMPLES) {
      ecgSamples[sampleIndex] = ecgBpm;
      ppgSamples[sampleIndex] = ppgBpm;
      tempSamples[sampleIndex] = bodyTemp;
      sampleIndex++;
    }
  }

  if (now >= BASELINE_END_MS || sampleIndex >= MAX_SAMPLES) {
    computeBaselineStats();
    baselineDone = true;

    Serial.println("[BASELINE] Completed");
    Serial.print("ECG mean: "); Serial.println(ecgMean, 1);
    Serial.print("PPG mean: "); Serial.println(ppgMean, 1);
    Serial.print("TEMP mean: "); Serial.println(tempMean, 2);
    Serial.print("Combined base: "); Serial.println(combinedBpmBase, 1);
    Serial.print("Combined threshold: "); Serial.println(combinedThreshold, 1);
  }
}

void updateDangerState() {
  if (!baselineDone) {
    warningFlag = false;
    activeFlag = false;
    dangerOngoing = false;
    return;
  }

  bool dangerCondition = combinedBpm > 0.0f && combinedBpm < combinedThreshold;

  if (dangerCondition) {
    if (!dangerOngoing) {
      dangerOngoing = true;
      dangerStartMs = millis();
    }

    unsigned long elapsed = millis() - dangerStartMs;
    warningFlag = elapsed >= WARNING_DELAY_MS && elapsed < ACTIVE_DELAY_MS;
    activeFlag = elapsed >= ACTIVE_DELAY_MS;
  } else {
    dangerOngoing = false;
    warningFlag = false;
    activeFlag = false;
  }
}

// -----------------------------------------------------------------------------
// Motor control
// -----------------------------------------------------------------------------

void motorStop() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  analogWrite(ENA_PIN, 0);
}

void motorExtend(uint8_t speed = 220) {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  analogWrite(ENA_PIN, speed);
}

void updateMotorState() {
  if (activeFlag) {
    motorExtend(220);
  } else {
    motorStop();
  }
}

// -----------------------------------------------------------------------------
// GPS and DR functions
// -----------------------------------------------------------------------------

void updateGps() {
  while (Serial1.available() > 0) {
    gps.encode(Serial1.read());
  }

  unsigned long now = millis();
  if (now - lastGpsCheckMs < GPS_CHECK_INTERVAL_MS) return;
  lastGpsCheckMs = now;

  if (gps.location.isValid() && gps.location.isUpdated()) {
    lastGpsLat = gps.location.lat();
    lastGpsLon = gps.location.lng();
    hasGpsFix = true;
    gpsLossCount = 0;

    Serial.print("[GPS] LAT: "); Serial.print(lastGpsLat, 6);
    Serial.print(", LON: "); Serial.println(lastGpsLon, 6);
  } else {
    if (hasGpsFix) gpsLossCount++;
    Serial.println("[GPS] Waiting for valid fix or update");
  }

  if (hasGpsFix && !gpsLost && gpsLossCount >= GPS_LOSS_THRESHOLD) {
    Serial.println("[GPS] Signal lost. Enter DR mode.");
    gpsLost = true;
    drActive = true;
    drStartMs = millis();

    sumAxWorld = 0.0;
    sumAyWorld = 0.0;
    sumAzWorld = 0.0;
    imuSampleCount = 0;

    roll = 0.0f;
    pitch = 0.0f;
    yawDeg = 0.0f;
    lastImuUs = micros();
  }
}

void processDeadReckoningImu() {
  unsigned long nowUs = micros();
  float dt = (nowUs - lastImuUs) / 1000000.0f;
  if (dt <= 0.0f) dt = 0.001f;
  lastImuUs = nowUs;

  if (!imu.dataReady()) return;
  imu.getAGMT();

  float axG = imu.accX();
  float ayG = imu.accY();
  float azG = imu.accZ();
  float gxDps = imu.gyrX();
  float gyDps = imu.gyrY();
  float gzDps = imu.gyrZ();

  float rollAcc = atan2f(ayG, azG);
  float pitchAcc = atan2f(-axG, sqrtf(ayG * ayG + azG * azG));

  float gx = gxDps * (PI / 180.0f);
  float gy = gyDps * (PI / 180.0f);

  roll += gx * dt;
  pitch += gy * dt;
  roll = ALPHA_RP * roll + (1.0f - ALPHA_RP) * rollAcc;
  pitch = ALPHA_RP * pitch + (1.0f - ALPHA_RP) * pitchAcc;

  yawDeg += gzDps * dt;

  float gBx = -sinf(pitch) * G_UNIT;
  float gBy = sinf(roll) * cosf(pitch) * G_UNIT;
  float gBz = cosf(roll) * cosf(pitch) * G_UNIT;

  float axLinBody = axG * G_UNIT - gBx;
  float ayLinBody = ayG * G_UNIT - gBy;
  float azLinBody = azG * G_UNIT - gBz;

  float yawRad = yawDeg * (PI / 180.0f);
  float cosYaw = cosf(yawRad);
  float sinYaw = sinf(yawRad);

  float axWorld = cosYaw * axLinBody - sinYaw * ayLinBody;
  float ayWorld = sinYaw * axLinBody + cosYaw * ayLinBody;
  float azWorld = azLinBody;

  sumAxWorld += axWorld;
  sumAyWorld += ayWorld;
  sumAzWorld += azWorld;
  imuSampleCount++;
}

void finishDeadReckoning() {
  if (imuSampleCount == 0) {
    Serial.println("[DR] No IMU samples collected");
    return;
  }

  float axMean = (float)(sumAxWorld / imuSampleCount);
  float ayMean = (float)(sumAyWorld / imuSampleCount);

  float headingAccDeg = atan2f(ayMean, axMean) * 180.0f / PI;
  float gyroYawNorm = normalizeDeg360(yawDeg);
  float fused = ALPHA_HEADING * gyroYawNorm + (1.0f - ALPHA_HEADING) * headingAccDeg;

  predictedDirection = normalizeDeg360(fused);
  strncpy(madeAngle, directionToText(predictedDirection), sizeof(madeAngle) - 1);
  madeAngle[sizeof(madeAngle) - 1] = '\0';

  Serial.println("[DR] Completed");
  Serial.print("PredictedDirection: "); Serial.println(predictedDirection, 2);
  Serial.print("MADEANGLE: "); Serial.println(madeAngle);
}

// -----------------------------------------------------------------------------
// BLE transmit
// -----------------------------------------------------------------------------

void notifyBlePacket() {
  if (!bleServer || !bleCharacteristic || bleServer->getConnectedCount() == 0) return;

  char packet[220];
  snprintf(packet, sizeof(packet),
           "LAT:%.6f,LON:%.6f,COMB_BPM:%.1f,TEMP:%.2f,PredictedDirection:%.2f,ACTIVE:%s,WARNING:%s,BASE_TEMP:%.2f,BASE_COMB_BPM:%.1f,MADEANGLE:%s,PPG_BPM:%.1f",
           lastGpsLat,
           lastGpsLon,
           combinedBpm,
           bodyTemp,
           predictedDirection,
           activeFlag ? "YES" : "NO",
           warningFlag ? "YES" : "NO",
           tempMean,
           combinedBpmBase,
           madeAngle,
           ppgBpm);

  bleCharacteristic->setValue((uint8_t*)packet, strlen(packet));
  bleCharacteristic->notify();

  static uint8_t debugCount = 0;
  debugCount++;
  if (debugCount % 5 == 0) {
    Serial.println(packet);
  }
}

// -----------------------------------------------------------------------------
// Setup and loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("[SYSTEM] Smart Life Vest firmware start");

  randomSeed(analogRead(ECG_PIN));

  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Wire.begin();
  delay(50);

  if (!beginIMU()) {
    Serial.println("[ERROR] ICM-20948 connection failed");
    while (1) delay(1000);
  }
  Serial.println("[OK] ICM-20948 connected");

  if (!ppg.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("[ERROR] MAX30102/MAX30105 connection failed");
    while (1) delay(1000);
  }
  ppg.setup();
  ppg.setPulseAmplitudeRed(0x1F);
  ppg.setPulseAmplitudeIR(0x1F);
  Serial.println("[OK] PPG sensor connected");

  pinMode(ENA_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  motorStop();

  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);

  BLEDevice::init(BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new LifeVestServerCallbacks());

  BLEService* service = bleServer->createService(SERVICE_UUID);
  bleCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  bleCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  Serial.println("[OK] BLE advertising started");

  lastImuUs = micros();
  lastGpsCheckMs = millis();
}

void loop() {
  unsigned long now = millis();

  updateGps();

  if (now - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = now;

    if (!drActive) {
      float tiltDeg;
      if (readTiltDeg(tiltDeg)) {
        tiltDegEma = 0.12f * tiltDeg + 0.88f * tiltDegEma;
      }
    }

    ecgBpm = processEcgAndGetBpm();
    ppgBpm = processPpgAndGetBpm();

    static float lastCombinedBpm = 0.0f;
    float combinedInstant = computeCombinedBpm(ecgBpm, ppgBpm);
    combinedBpm = 0.3f * combinedInstant + 0.7f * lastCombinedBpm;
    lastCombinedBpm = combinedBpm;

    bodyTemp = readTemperatureTMP102();

    updateBaselineCollection();
    updateDangerState();
    updateMotorState();
  }

  if (drActive) {
    processDeadReckoningImu();
    if (millis() - drStartMs >= DR_DURATION_MS) {
      finishDeadReckoning();
      drActive = false;
    }
  }

  if (now - lastBleNotifyMs >= BLE_NOTIFY_INTERVAL_MS) {
    lastBleNotifyMs = now;
    notifyBlePacket();
  }
}
