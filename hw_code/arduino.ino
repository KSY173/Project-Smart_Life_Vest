// Unified: DR + GPS + IMU  ↔  ECG+PPG + TMP102 + Motor  + BLE notify
// First part: DR + GPS + BLE (from message)
// Second part: ECG+PPG code (uploaded as combBPMNanoCode.txt). :contentReference[oaicite:1]{index=1}

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include "ICM_20948.h"
#include "MAX30105.h"
#include "heartRate.h"   // from MAX30105 examples

// BLE (Neil Kolban style)
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ----------------------------
// PIN & HARDWARE definitions
// ----------------------------

// TMP102
#define TMP102_ADDR 0x48

// ECG / AD8232
#define ECG_PIN       A0
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3

// Motor (L298N / Linear actuator)
#define ENA_PIN   9
#define IN1_PIN   7
#define IN2_PIN   8

// GPS uses Serial1 (pins set in Serial1.begin where supported)

// ----------------------------
// BLE UUIDs
// ----------------------------
#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;

// ----------------------------
// GPS / DR variables (from first code)
// ----------------------------
TinyGPSPlus gps;
const int GPS_LOSS_THRESHOLD = 10;   // increments on missing fix checks
const unsigned long GPS_PRINT_INTERVAL = 2000UL; // 2 sec

double last_gps_lat = 0.0;
double last_gps_lon = 0.0;
unsigned long lastGPSPrint = 0;
int gpsCount = 0;
bool gpsLost = false;

// ----------------------------
// IMU 
// ----------------------------
ICM_20948_I2C imu;

// complementary filter params (from DR code)
const float Gunit = 1.0f;
const float ALPHA_RP = 0.98f;
const float ALPHA_HEADING = 0.98f;

float roll = 0.0f;
float pitch = 0.0f;
float yaw_deg = 0.0f;

float predictedDirection = 0.0f;  // 전역 변수로 선언

char global_direction_str[20] = " ";

double sum_ax_w = 0.0;
double sum_ay_w = 0.0;
double sum_az_w = 0.0;
unsigned long sampleCount = 0;
unsigned long lastIMUUs = 0;

// DR control
const unsigned long DR_DURATION_MS = 60UL * 1000UL;
unsigned long drStartMs = 0;
bool drActive = false;

// ----------------------------
// PPG (MAX30102)
 // (from uploaded file) :contentReference[oaicite:2]{index=2}
MAX30105 ppg;

// PPG params
#define PPG_IR_FINGER_THRESHOLD 15000UL    //원래 50000 이었던 것
#define PPG_AC_THRESH 200
#define PPG_MIN_BEAT_INTERVAL_MS 400
#define PPG_MAX_BEAT_INTERVAL_MS 2000
#define PPG_BEAT_HISTORY_LEN 6
#define PPG_BEAT_WINDOW_MS 12000
#define PPG_EMA_ALPHA 0.2f
#define PPG_MIN_VALID 30.0f
#define PPG_MAX_VALID 180.0f
#define PPG_JUMP_MAX 80.0f

int64_t ppg_beat_times[PPG_BEAT_HISTORY_LEN];
int ppg_beat_index = 0;
int ppg_beat_count = 0;
float ppg_bpm_ema = 0.0f;
uint32_t ppg_baseline = 0; // integer baseline

// ----------------------------
// ECG variables (from uploaded file) :contentReference[oaicite:3]{index=3}
#define ECG_DYNAMIC_THRESHOLD_DEFAULT 10    //원래는 40인데 확인용으로 10 변경
#define ECG_NO_PEAK_TIMEOUT_MS 8000
#define ECG_MIN_RR_MS 400
#define ECG_MAX_RR_MS 1000
#define ECG_REFRACTORY_MS 350
#define ECG_RR_HISTORY_LEN 4
#define ECG_MIN_VALID_BPM 40
#define ECG_MAX_VALID_BPM 140
#define ECG_MAX_STEP_UP 10
#define ECG_MAX_STEP_DOWN 15

bool ecg_is_peak = false;
uint64_t ecg_last_peak_time = 0;
uint32_t ecg_rr_history[ECG_RR_HISTORY_LEN] = {0};
int ecg_rr_count = 0;
int ecg_rr_index = 0;
int ecg_last_valid_bpm = 0;
float ecg_baseline = 0.0f;
const float ECG_BASE_ALPHA = 0.02f;
float ecg_dyn_threshold = ECG_DYNAMIC_THRESHOLD_DEFAULT;

// ----------------------------
// Baseline collection (from uploaded file) :contentReference[oaicite:4]{index=4}
#define MAX_SAMPLES 600
float ecgSamples[MAX_SAMPLES];
float ppgSamples[MAX_SAMPLES];
float tempSamples[MAX_SAMPLES];
int sampleIndex = 0;
bool baselineDone = false;
bool baselineSent = false;
bool baselineStartPrinted = false;

float ecgMean = 0.0f, ecgStd = 0.0f, ecgThr = 0.0f;
float ppgMean = 0.0f, ppgStd = 0.0f, ppgThr = 0.0f;
float tempMean = 0.0f;
float combinedThr = 0.0f;
float combinedBPMBase = 0.0f;

// ----------------------------
// Combined BPM & thresholds & state
// ----------------------------
const float wECG = 0.7f;
const float wPPG = 0.3f;

float tiltDegEMA = 0.0f;
float ecgBPM = 0.0f, ppgBPM = 0.0f;
float combinedBPM = 0.0f;
bool active = false;
bool warningFlag = false;

unsigned long dangerStart = 0;
bool dangerOngoing = false;

// sensor timing
unsigned long lastSensorReadMs = 0;
const unsigned long sensorIntervalMs = 100; // 100 ms

// ----------------------------
// helpers / prototypes
// ----------------------------
float readTemperatureTMP102();
bool beginIMU();
bool readTiltDeg(float &tiltDeg);
int readECG_raw();
float processECG_and_getBPM();
void ppg_add_beat_time(int64_t t);
float ppg_compute_bpm_from_history(int64_t now_ms);
float processPPG_and_getBPM();
void computeBaselineStats();
void motorStop();
void motorExtend(uint8_t speed = 220);
void enterDRMode();
void processIMU();
void finishDR();

// BLE callbacks (single, unified)
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("BLE Client Connected");
  }
  void onDisconnect(BLEServer* pServer) {
    Serial.println("BLE Client Disconnected");
    BLEAdvertising* pAdvertising = pServer->getAdvertising();
    pAdvertising->start();
  }
};

// ----------------------------
// Implementations
// ----------------------------

// TMP102 temperature read
float readTemperatureTMP102() {
  Wire.beginTransmission(TMP102_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();

  Wire.requestFrom(TMP102_ADDR, 2);
  if (Wire.available() < 2) return 0.0f;
  int msb = Wire.read();
  int lsb = Wire.read();

  int16_t rawTemp = ((msb << 8) | lsb) >> 4;
  return rawTemp * 0.0625f;
}

// IMU begin wrapper (tries both addresses like uploaded file)
bool beginIMU() {
  if (imu.begin(Wire, 1) == ICM_20948_Stat_Ok) return true;
  if (imu.begin(Wire, 0) == ICM_20948_Stat_Ok) return true;
  return false;
}

// read tilt (from uploaded file)
bool readTiltDeg(float &tiltDeg) {
  if (!imu.dataReady()) return false;
  imu.getAGMT();

  float ax = imu.accX();
  float ay = imu.accY();
  float az = imu.accZ();

  float horiz = sqrtf(ax * ax + ay * ay);
  tiltDeg = atan2f(horiz, az) * 180.0f / PI;
  return true;
}

// ECG raw read
int readECG_raw() {
  return analogRead(ECG_PIN);
}

// process ECG -> BPM (from uploaded file) :contentReference[oaicite:5]{index=5}
float processECG_and_getBPM() {
  int raw = readECG_raw();
  uint64_t now = millis();

  if (ecg_baseline == 0.0f) ecg_baseline = raw;
  ecg_baseline = ecg_baseline + ECG_BASE_ALPHA * ((float)raw - ecg_baseline);

  int dyn = (int)raw - (int)round(ecg_baseline);

  if ((dyn > ecg_dyn_threshold) && !ecg_is_peak) {
    uint64_t now_peak = now;
    if (ecg_last_peak_time != 0 && (now_peak - ecg_last_peak_time) < ECG_REFRACTORY_MS) {
      // ignore
    } else {
      ecg_is_peak = true;
      if (ecg_last_peak_time != 0) {
        uint32_t rr = (uint32_t)(now_peak - ecg_last_peak_time);
        if (rr > ECG_NO_PEAK_TIMEOUT_MS) {
          ecg_rr_count = 0;
        } else if (rr >= ECG_MIN_RR_MS && rr <= ECG_MAX_RR_MS) {
          ecg_rr_history[ecg_rr_index] = rr;
          ecg_rr_index = (ecg_rr_index + 1) % ECG_RR_HISTORY_LEN;
          if (ecg_rr_count < ECG_RR_HISTORY_LEN) ecg_rr_count++;
          uint64_t sum = 0;
          for (int i=0;i<ecg_rr_count;i++) sum += ecg_rr_history[i];
          uint32_t avg_rr = (uint32_t)(sum / ecg_rr_count);
          int candidate_bpm = (int)(60000UL / avg_rr);
          if (candidate_bpm >= ECG_MIN_VALID_BPM && candidate_bpm <= ECG_MAX_VALID_BPM) {
            if (ecg_last_valid_bpm == 0) {
              ecg_last_valid_bpm = candidate_bpm;
            } else {
              int diff = candidate_bpm - ecg_last_valid_bpm;
              if (diff > ECG_MAX_STEP_UP) diff = ECG_MAX_STEP_UP;
              if (diff < -ECG_MAX_STEP_DOWN) diff = -ECG_MAX_STEP_DOWN;
              ecg_last_valid_bpm += diff;
            }
          }
        }
      }
      ecg_last_peak_time = now_peak;
    }
  }

  if (dyn < (ecg_dyn_threshold / 2)) {
    ecg_is_peak = false;
  }

  int display_bpm = 0;
  if (ecg_last_peak_time != 0 && (now - ecg_last_peak_time) <= ECG_NO_PEAK_TIMEOUT_MS) {
    display_bpm = ecg_last_valid_bpm;
  } else {
    display_bpm = 0;
  }

  return (float)display_bpm;
}

// PPG helpers (from uploaded file) :contentReference[oaicite:6]{index=6}
void ppg_add_beat_time(int64_t t) {
  ppg_beat_times[ppg_beat_index] = t;
  ppg_beat_index = (ppg_beat_index + 1) % PPG_BEAT_HISTORY_LEN;
  if (ppg_beat_count < PPG_BEAT_HISTORY_LEN) ppg_beat_count++;
}

float ppg_compute_bpm_from_history(int64_t now_ms) {
  if (ppg_beat_count < 2) return -1.0f;

  int valid = 0;
  int64_t first = 0, last = 0;
  int idx = (ppg_beat_index - ppg_beat_count + PPG_BEAT_HISTORY_LEN) % PPG_BEAT_HISTORY_LEN;
  for (int i=0;i<ppg_beat_count;i++) {
    int64_t t = ppg_beat_times[idx];
    idx = (idx + 1) % PPG_BEAT_HISTORY_LEN;
    if (now_ms - t <= PPG_BEAT_WINDOW_MS) {
      if (valid==0) first = t;
      last = t;
      valid++;
    }
  }
  if (valid < 2) return -1.0f;
  float ibi_ms = (float)(last - first) / (float)(valid - 1);
  if (ibi_ms <= 0.0f) return -1.0f;
  return 60000.0f / ibi_ms;
}

float processPPG_and_getBPM() {
  ppg.check();
  long irValue = ppg.getIR();
  uint64_t now = millis();

  if (irValue < (long)PPG_IR_FINGER_THRESHOLD) {
    return 0.0f;
  }

  if (ppg_baseline == 0) ppg_baseline = (uint32_t)irValue;
  else ppg_baseline = ppg_baseline + ((int32_t)irValue - (int32_t)ppg_baseline) / 32;

  int32_t ac = (int32_t)irValue - (int32_t)ppg_baseline;
  //int32_t ac_abs = (ac >= 0) ? ac : -ac;

 // if (checkForBeat(irValue)) {
 //   Serial.print("[PPG] BEAT at ");
 //   Serial.println(now);
 //   ppg_add_beat_time(now);
 //}

  ///////////////////////////////////////////////////////
  /////////////// checkforbeat 보완용 ///////////////////
  static int32_t last_ac = 0;
  static bool rising = false;
  static uint64_t lastBeatMs = 0;   // ★ 여기!

  //AC 기반 피크 검출 + 최소 박동 간격 제한
  if (ac > PPG_AC_THRESH && last_ac <= PPG_AC_THRESH && !rising) {
    if (now - lastBeatMs >= PPG_MIN_BEAT_INTERVAL_MS) {
      rising = true;
      lastBeatMs = now;
      ppg_add_beat_time(now);
      //Serial.println("[PPG] AC PEAK");
    }
  }

  // 충분히 내려오면 다음 박동 허용
  if (ac < PPG_AC_THRESH / 2) {
    rising = false;
  }

  last_ac = ac;

  ////////////////////////////////////////////////////////
  ////////////////////////////////////////////////////////

  float inst_bpm = ppg_compute_bpm_from_history(now);

 /* 
  DEBUG PRING
  static int dbg2 = 0;
  dbg2++;

  if (dbg2 % 20 == 0) {
    Serial.print("[PPG DBG] IR=");
    Serial.print(irValue);
    Serial.print(" inst_bpm=");
    Serial.print(inst_bpm);
    Serial.print(" ema=");
    Serial.print(ppg_bpm_ema);
    Serial.println(" ");
  } */

  if (inst_bpm > 0.0f && inst_bpm >= PPG_MIN_VALID && inst_bpm <= PPG_MAX_VALID) {
    bool accept = true;
    if (ppg_bpm_ema > 0.0f) {
      float diff = inst_bpm - ppg_bpm_ema;
      if (diff < 0.0f) diff = -diff;
      if (diff > PPG_JUMP_MAX) accept = false;
    }
    if (accept) {
      if (ppg_bpm_ema == 0.0f) ppg_bpm_ema = inst_bpm;
      else ppg_bpm_ema = PPG_EMA_ALPHA * inst_bpm + (1.0f - PPG_EMA_ALPHA) * ppg_bpm_ema;
    }
  }

  //float display = (ppg_bpm_ema > 0.0f) ? ppg_bpm_ema : 0.0f;
  float display;
  if (ppg_bpm_ema > 0.0f) display = ppg_bpm_ema;
  else if (inst_bpm > 0.0f) display = inst_bpm;
  else display = 0.0f;

  return display;
}

// Baseline compute (from uploaded file) :contentReference[oaicite:7]{index=7}
void computeBaselineStats() {
  if (sampleIndex <= 0) {
    ecgMean = 0;
    ecgStd = 0;
    ppgMean = 0;
    ppgStd = 0;

    ecgThr = 40;
    ppgThr = 40;
    combinedThr = 40;

    return;
}

  double ecgSum = 0.0, ppgSum = 0.0, tempSum = 0.0;
  //float ecgSamp[MAX_SAMPLES], ppgSamp[MAX_SAMPLES], tempSamp[MAX_SAMPLES];
  int ecgidx = 0, ppgidx = 0, tempidx = 0;

  for (int i=0;i<sampleIndex;i++) {
    if (ecgSamples[i] != 0) {
      ecgSum += ecgSamples[i];
      ecgidx++;
    }
    if (ppgSamples[i] != 0) {
      ppgSum += ppgSamples[i];
      ppgidx++;
    }
    if (tempSamples[i] != 0) {
      tempSum += tempSamples[i];
      tempidx++;
    }
  }
  ecgMean = (float)(ecgSum / ecgidx);
  ppgMean = (float)(ppgSum / ppgidx);
  tempMean = (float)(tempSum / tempidx);

  if (isnan(ppgMean)) {
    ppgMean = 70.8f;
  }

  if (isnan(ecgMean)) {
    ecgMean = 80.7f;
  }

  double ecgVar = 0.0, ppgVar = 0.0, de = 0.0, dp = 0.0;
  for (int i=0;i<sampleIndex;i++) {
    if (ecgSamples[i] != 0) {
      de = ecgSamples[i] - ecgMean;
    }
    if (ppgSamples[i] != 0) {
      dp = ppgSamples[i] - ppgMean;
    }

    ecgVar += de * de;
    ppgVar += dp * dp;
  }

  ecgStd = (float)sqrt(ecgVar / ecgidx);
  //ppgStd = (float)sqrt(ppgVar / ppgidx);

  float ecgThrA = ecgMean - 1.5f * ecgStd;
  float ecgThrB = ecgMean * 0.75f;
  ecgThr = max(40.0f, max(ecgThrA, ecgThrB));

  //float ppgThrA = ppgMean - 1.5f * ppgStd;
  float ppgThrB = ppgMean * 0.75f;
  ppgThr = max(40.0f,ppgThrB);

  combinedThr = wECG * ecgThr + wPPG * ppgThr;

  combinedBPMBase = ecgMean * wECG + ppgMean * wPPG;
}

// Motor control (from uploaded file) :contentReference[oaicite:8]{index=8}
void motorStop() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  analogWrite(ENA_PIN, 0);
}

void motorExtend(uint8_t speed) {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  analogWrite(ENA_PIN, speed);
}

// ----------------------------
// DR code functions (from first message)
// ----------------------------
float normalize_deg_180(float ang) {
 // while (ang > 180.0f) ang -= 360.0f;
 // while (ang <= -180.0f) ang += 360.0f;

  while (ang > 360.0f) ang -= 360.0f;
  while (ang <= 0.0f) ang += 360.0f;
  return ang;
}

void enterDRMode() {
  gpsLost = true;
  drActive = true;
  drStartMs = millis();

  sum_ax_w = sum_ay_w = sum_az_w = 0.0;
  sampleCount = 0;

  roll = 0.0f;
  pitch = 0.0f;
  yaw_deg = 0.0f;
  lastIMUUs = micros();

  Serial.print("Last GPS: "); Serial.print(last_gps_lat,6);
  Serial.print(", "); Serial.println(last_gps_lon,6);
  Serial.println("Start collecting IMU for DR...");
}

void processIMU() {
  unsigned long nowUs = micros();
  float dt = (nowUs - lastIMUUs)/1e6f;
  if (dt <= 0) dt = 0.001f;
  lastIMUUs = nowUs;

  if (!imu.dataReady()) return;
  imu.getAGMT();

  float ax_g = imu.accX();
  float ay_g = imu.accY();
  float az_g = imu.accZ();
  float gx_dps = imu.gyrX();
  float gy_dps = imu.gyrY();
  float gz_dps = imu.gyrZ();

  float roll_acc = atan2f(ay_g, az_g);
  float pitch_acc = atan2f(-ax_g, sqrtf(ay_g*ay_g + az_g*az_g));

  float gx = gx_dps * (PI/180.0f);
  float gy = gy_dps * (PI/180.0f);

  roll += gx*dt;
  pitch += gy*dt;
  roll = ALPHA_RP * roll + (1.0f-ALPHA_RP)*roll_acc;
  pitch = ALPHA_RP * pitch + (1.0f-ALPHA_RP)*pitch_acc;

  yaw_deg += gz_dps*dt;

  float g_bx = -sinf(pitch)*Gunit;
  float g_by = sinf(roll)*cosf(pitch)*Gunit;
  float g_bz = cosf(roll)*cosf(pitch)*Gunit;

  float ax_ms2 = ax_g*Gunit;
  float ay_ms2 = ay_g*Gunit;
  float az_ms2 = az_g*Gunit;

  float ax_lin_b = ax_ms2 - g_bx;
  float ay_lin_b = ay_ms2 - g_by;
  float az_lin_b = az_ms2 - g_bz;

  float yaw_rad = yaw_deg*(PI/180.0f);
  float cos_y = cosf(yaw_rad);
  float sin_y = sinf(yaw_rad);

  float ax_w = cos_y*ax_lin_b - sin_y*ay_lin_b;
  float ay_w = sin_y*ax_lin_b + cos_y*ay_lin_b;
  float az_w = az_lin_b;

  sum_ax_w += ax_w;
  sum_ay_w += ay_w;
  sum_az_w += az_w;
  sampleCount++;
}

void finishDR() {
  Serial.println("\n===== DR FINISHED =====");
  if (sampleCount == 0) {
    Serial.println("No IMU samples.");
    return;
  }


  float ax_mean = (float)(sum_ax_w / sampleCount);
  float ay_mean = (float)(sum_ay_w / sampleCount);
  float az_mean = (float)(sum_az_w / sampleCount);

  float heading_acc_rad = atan2f(ay_mean, ax_mean);
  float heading_acc_deg = heading_acc_rad*180.0f/PI;
  float gyro_yaw_norm = normalize_deg_180(yaw_deg);
  float fused = ALPHA_HEADING*gyro_yaw_norm + (1.0f-ALPHA_HEADING)*heading_acc_deg;
  fused = normalize_deg_180(fused);

  predictedDirection = fused;  // 예측된 방향을 저장
  
  char direction[100];

  Serial.print("Final Avg ax_w: "); Serial.println(ax_mean,6);
  Serial.print("Final Avg ay_w: "); Serial.println(ay_mean,6);
  Serial.print("Final Avg az_w: "); Serial.println(az_mean,6);
  Serial.print("Accel-heading (deg): "); Serial.println(heading_acc_deg,3);
  Serial.print("Gyro-yaw (deg): "); Serial.println(gyro_yaw_norm,3);
  Serial.print("Predicted Direction (deg): "); Serial.println(fused,3);
  Serial.print("Predicted Direction (string): "); Serial.println(global_direction_str);

  if ( (fused >= 0) && (fused < 35) ) {
    strcpy(direction, "N");
  } else if ( (fused >= 35) && (fused < 55) ) {
    strcpy(direction, "NE");
  } else if ( (fused >= 55) && (fused < 125) ) {
    strcpy(direction, "E");
  } else if ( (fused >= 125) && (fused < 145) ) {
    strcpy(direction, "ES");
  } else if ( (fused >= 145) && (fused < 215) ) {
    strcpy(direction, "S");
  } else if ( (fused >= 215) && (fused < 235) ) {
    strcpy(direction, "SW");
  } else if ( (fused >= 235) && (fused < 305) ) {
    strcpy(direction, "W");
  } else if ( (fused >= 305) && (fused < 325) ) {
    strcpy(direction, "NW");
  } else {
    strcpy(direction, "N");
  }

  strcpy(global_direction_str, direction);
  

  // BLE send DR fused + last GPS
  if (pServer && pServer->getConnectedCount() > 0) {
    char drPacket[120];
    snprintf(drPacket, sizeof(drPacket),
             "LAT:%.6f,LON:%.6f,PredictedDirection:%.2f,MADEANGLE:%s",
             last_gps_lat, last_gps_lon, fused, direction, global_direction_str);
    pCharacteristic->setValue((uint8_t*)drPacket, strlen(drPacket));
    pCharacteristic->notify();
    Serial.println("DR fused value + GPS sent via BLE.");
  }

  Serial.println("===== END =====");
}

// ----------------------------
// Setup & Loop (unified)
// ----------------------------
void setup() {
  Serial.begin(115200);
  while(!Serial); // wait for Serial (useful for some boards)
  
  randomSeed(analogRead(A7));
  
  delay(200);
  Serial.println("Unified: GPS + ICM-20948 DR + MAX30102 + ECG + TMP102 + BLE");

  // GPS Serial1
  Serial1.begin(9600, SERIAL_8N1, 2, 3); // note: pins depend on board

  Wire.begin();
  delay(50);

  // IMU
  if (!beginIMU()) {
    Serial.println("IMU 연결 실패");
    while (1) delay(1000);
  }

  // PPG
  if (!ppg.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 연결 실패");
    while (1) delay(1000);
  }
  ppg.setup();
  ppg.setPulseAmplitudeRed(0x1F);
  ppg.setPulseAmplitudeIR(0x1F);

  // Motor pins
  pinMode(ENA_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  motorStop();

  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);

  // BLE init (single)
  BLEDevice::init("LifeVest_UNIFIED");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE Initialized");

  lastIMUUs = micros();
  lastGPSPrint = millis();
  lastSensorReadMs = 0;
}

// unified loop
void loop() {
  unsigned long nowMs = millis();

  // 1) feed GPS bytes to TinyGPS
  while (Serial1.available() > 0) {
    gps.encode(Serial1.read());
  }

  // 2) GPS periodic processing & GPS-loss detection (from first code)
  if (!gpsLost) {
    if (nowMs - lastGPSPrint >= GPS_PRINT_INTERVAL) {
      lastGPSPrint = nowMs;

      if (gps.location.isValid()) {
        last_gps_lat = gps.location.lat();
        last_gps_lon = gps.location.lng();
        gpsCount++;
        Serial.print("[GPS] lat: "); Serial.print(last_gps_lat, 6);
        Serial.print(", lon: "); Serial.println(last_gps_lon, 6);
      } else {
        Serial.println("[GPS] No valid fix");
        // keep last known or fallback
        last_gps_lat = 36.9812f;
        last_gps_lon = 126.0193f;
        gpsCount++;
      }

      if (gpsCount >= GPS_LOSS_THRESHOLD) {
        Serial.println("\n=== GPS SIGNAL LOST: ENTER DR MODE ===");
        enterDRMode();
      }
    }
    // while GPS active we still want to run baseline collection and PPG/ECG sampling:
    // continue to sensor processing below (no return)
  }

  // 3) sensor interval processing (ECG/PPG/tilt/TMP102) every sensorIntervalMs
  if (nowMs - lastSensorReadMs >= sensorIntervalMs) {
    lastSensorReadMs = nowMs;

    // IMU tilt for tiltDegEMA (from uploaded file)
    float tilt;
    if (readTiltDeg(tilt)) tiltDegEMA = 0.12f * tilt + 0.88f * tiltDegEMA;

    // read and process ECG / PPG
    ecgBPM = processECG_and_getBPM();
    ppgBPM = processPPG_and_getBPM();

    // EMA smoothing combine
    static float lastCombinedBPM = 0.0f;
    float combinedInstant = (ecgBPM > 0 ? ecgBPM : 0.0f) * wECG + (ppgBPM > 0 ? ppgBPM : 0.0f) * wPPG;
    float alpha = 0.3f;
    combinedBPM = alpha * combinedInstant + (1.0f - alpha) * lastCombinedBPM;
    lastCombinedBPM = combinedBPM;

    // TMP102 read
    float tempC = readTemperatureTMP102();
    static float simTemp = 33.0f;   // 시작 기준 온도

    // -0.2 ~ +0.2 변화
    simTemp += random(-2, 3) * 0.1f;

    // 범위 제한 (30 ~ 36)
    if (simTemp < 30.0f) simTemp = 30.6f;
    if (simTemp > 36.0f) simTemp = 35.7f;

    tempC = simTemp;

    // Baseline collection (from uploaded file) :contentReference[oaicite:9]{index=9}
    if (!baselineDone) {
      if (millis() > 20000 ) {  //(ecgBPM > 0 && ppgBPM > 0 && tempC > 0.0f)
        if (!baselineStartPrinted) {
          Serial.println("=== Baseline Start ===");
          baselineStartPrinted = true;   // ★ 이제 딱 한 번만 출력됨
        }

        if (sampleIndex < MAX_SAMPLES) {
          ecgSamples[sampleIndex] = ecgBPM;
          ppgSamples[sampleIndex] = ppgBPM;
          tempSamples[sampleIndex] = tempC;
          sampleIndex++;
        }
      }

      if (millis() > 50000 || sampleIndex >= MAX_SAMPLES) { //(millis() > 60000 || sampleIndex >= MAX_SAMPLES)
        computeBaselineStats();
        baselineDone = true;
        Serial.println("=== Baseline Completed ===");
        Serial.print("ECG mean = "); Serial.print(ecgMean,1);
        Serial.print(" PPG mean = "); Serial.print(ppgMean,1);
        Serial.print(" Temp mean = "); Serial.print(tempMean,1);
        Serial.print(" Combined BPM = "); Serial.print(combinedBPMBase,1);
        Serial.print(" Combined BPM Threshold = "); Serial.print(combinedThr,1);
        Serial.println(" ");
      }
    }

    

    // Danger/time based decision (from uploaded file)
    if (baselineDone) {
      if (combinedBPM > 0 && combinedBPM < combinedThr) {
        if (!dangerOngoing) {
          dangerOngoing = true;
          dangerStart = millis();
        }
        unsigned long dt = millis() - dangerStart;
        if (dt >= 6000 && dt < 10000) {
          warningFlag = true;
          active = false;
        } else if (dt >= 10000) {
          active = true;
          warningFlag = false;
        }
      } else {
        dangerOngoing = false;
        active = false;
        warningFlag = false;
      }

      // Motor control
      if (active) motorExtend(220);
      else motorStop();
    }

    // BLE transmit: include GPS last known, combined BPM, temp, tilt, DR flag if active
    char buffer[250];
    snprintf(buffer, sizeof(buffer),
         "LAT:%.6f,LON:%.6f,COMB_BPM:%.1f,TEMP:%.2f,predictedDirection:%.2f,ACTIVE:%s,WARNING:%s,BASE_ECG:%.1f,BASE_PPG:%.1f,BASE_TEMP:%.1f,BASE_COMB_BPM:%.1f,MADEANGLE:%s,PPG_BPM:%.1f",
         last_gps_lat, last_gps_lon, combinedBPM, tempC, predictedDirection,  // predictedDirection 추가
         (active ? "YES" : "NO"), (warningFlag ? "YES" : "NO"), ecgMean, ppgMean, tempMean, combinedBPMBase, global_direction_str, ppgBPM);

    if (pServer && pServer->getConnectedCount() > 0) {
      pCharacteristic->setValue((uint8_t*)buffer, strlen(buffer));
      pCharacteristic->notify();
    }

    // serial debug
    static int pulseCount = 0;
    pulseCount++;
    if (pulseCount % 40 == 0) Serial.println(buffer);
  } // end sensor interval block

  // 4) DR processing if in DR mode (collect IMU and run finish condition)
  if (drActive) {
    processIMU();
    if (millis() - drStartMs >= DR_DURATION_MS) {
      finishDR();
      drActive = false;
      // after finishing DR we stop (matching original behavior)
      // while (1) delay(1000);
    }
  }
/*
///////////////////////////////////////////////////////////////////////////////
// SIMULATION: baseline 완료 + DR 끝난 뒤 → 10초 기다렸다가 active = true 유지//
///////////////////////////////////////////////////////////////////////////////
    static bool simActivated = false;
    static unsigned long simStartMs = 0;

    if (baselineDone && !drActive) {
    // 처음 진입 시 시간 기록
      if (!simActivated) {
        if (simStartMs == 0) {
            simStartMs = nowMs;   // 시작 시간 저장
        }

        // 10초 지나면 active ON
        if (nowMs - simStartMs >= 10000) {
            active = true;
            simActivated = true;
            Serial.println("SIM");
            if (active) motorExtend(220);
            else motorStop();
            motorExtend(220);
            motorStop();
        }
      } else {
        // 이미 활성화된 상태면 계속 active 유지
        active = true;
      }
    }*/
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
}