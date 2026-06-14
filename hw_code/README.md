# Smart Life Vest - Arduino 

본 프로젝트는 자동 팽창식 스마트 구명조끼의 임베디드 펌웨어입니다. GPS, IMU, ECG, PPG, 온도 센서 데이터를 통합하여 사용자의 위치, 이동 방향, 생체 신호, 위험 상태를 판단하고, BLE Notify를 통해 Flutter 앱으로 상태 정보를 전송합니다.

---

## 1. Project Overview

Smart Life Vest Arduino Firmware는 다음 기능을 수행합니다.

- GPS 기반 위도/경도 수집
- GPS 신호 손실 시 IMU 기반 Dead Reckoning 방향 추정
- AD8232 ECG 센서 기반 심박수 측정
- MAX30102/MAX30105 PPG 센서 기반 심박수 측정
- TMP102 온도 센서 기반 온도 측정
- ECG와 PPG를 결합한 통합 BPM 계산
- baseline 기반 위험 상태 판단
- `WARNING`, `ACTIVE` 상태 BLE 전송
- `ACTIVE` 상태 발생 시 L298N 모터 드라이버를 통한 팽창 메커니즘 구동
- Flutter 앱과 연동되는 BLE 텍스트 패킷 전송

---

## 2. Hardware Components

| Component | Purpose |
|---|---|
| Arduino Nano ESP32 | 메인 제어 보드 |
| GNSS / GPS Module | 사용자 위치 추적 |
| ICM-20948 IMU | 가속도, 자이로 기반 자세 및 방향 추정 |
| MAX30102 / MAX30105 | PPG 기반 심박수 측정 |
| AD8232 | ECG 기반 심박수 측정 |
| TMP102 | 온도 측정 |
| L298N Motor Driver | 모터 또는 리니어 액추에이터 제어 |
| Motor / Linear Actuator | 구명조끼 팽창 메커니즘 구동 |
| BLE | Flutter 앱으로 상태 데이터 전송 |

---

## 3. Pin Configuration

| Pin | Connected Module | Description |
|---|---|---|
| `A0` | AD8232 ECG | ECG 아날로그 신호 입력 |
| `D2` | AD8232 LO+ | ECG lead-off 감지 입력 |
| `D3` | AD8232 LO- | ECG lead-off 감지 입력 |
| `D7` | L298N IN1 | 모터 방향 제어 |
| `D8` | L298N IN2 | 모터 방향 제어 |
| `D9` | L298N ENA | 모터 PWM 속도 제어 |
| `D16` | GPS RX | GPS Serial1 RX |
| `D17` | GPS TX | GPS Serial1 TX |
| I2C SDA/SCL | ICM-20948, MAX30102/MAX30105, TMP102 | 센서 I2C 통신 |

```cpp
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3

#define GPS_RX_PIN    16
#define GPS_TX_PIN    17
```

---

## 4. Software Requirements

Arduino IDE 또는 PlatformIO에서 아래 라이브러리가 필요합니다.

- `TinyGPSPlus`
- `ICM_20948`
- `MAX30105`
- `Wire`
- `BLEDevice`
- `BLEServer`
- `BLEUtils`
- `BLE2902`

---

## 5. System Features

### 5.1 GPS Location Tracking

GPS 모듈에서 위도와 경도를 수신합니다. GPS 데이터는 `TinyGPSPlus` 라이브러리를 통해 파싱됩니다.

```cpp
TinyGPSPlus gps;
```

GPS 위치는 2초마다 확인됩니다.

```cpp
const unsigned long GPS_CHECK_INTERVAL_MS = 2000UL;
```

유효한 GPS 위치가 업데이트되면 `lastGpsLat`, `lastGpsLon`에 저장되고 BLE 패킷의 `LAT`, `LON` 값으로 전송됩니다.

---

### 5.2 GPS Loss Detection and Dead Reckoning

GPS 신호가 한 번이라도 정상적으로 수신된 후, 위치 업데이트가 연속으로 실패하면 GPS 손실 상태로 판단합니다.

```cpp
const uint8_t GPS_LOSS_THRESHOLD = 10;
```

GPS 손실이 감지되면 IMU 기반 Dead Reckoning 모드에 진입합니다.

```cpp
const unsigned long DR_DURATION_MS = 60000UL;
```

DR 모드에서는 ICM-20948의 가속도와 자이로 데이터를 약 60초 동안 수집하여 사용자의 이동 방향을 추정합니다. 추정 결과는 각도와 방향 문자열로 변환됩니다.

예시:

```text
PredictedDirection:85.23,MADEANGLE:E
```

---

### 5.3 ECG Heart Rate Measurement

AD8232 ECG 센서의 아날로그 신호를 읽고, R-peak를 기반으로 BPM을 계산합니다.

```cpp
#define ECG_PIN       A0
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3
```

ECG lead-off가 감지되면 ECG 값은 유효하지 않은 값으로 처리됩니다.

```cpp
if (digitalRead(LO_PLUS_PIN) == HIGH || digitalRead(LO_MINUS_PIN) == HIGH) {
  return 0;
}
```

---

### 5.4 PPG Heart Rate Measurement

MAX30102/MAX30105 센서의 IR 값을 기반으로 PPG 피크를 검출하고 BPM을 계산합니다.

```cpp
MAX30105 ppg;
```

PPG 센서 접촉 여부는 IR 값 기준으로 판단합니다.

```cpp
#define PPG_IR_FINGER_THRESHOLD 15000UL
```

---

### 5.5 Combined BPM Calculation

ECG와 PPG가 모두 유효할 때는 ECG 70%, PPG 30% 가중치를 적용하여 통합 BPM을 계산합니다.

```cpp
const float ECG_WEIGHT = 0.7f;
const float PPG_WEIGHT = 0.3f;
```

| ECG | PPG | Combined BPM |
|---|---|---|
| Valid | Valid | `0.7 * ECG + 0.3 * PPG` |
| Valid | Invalid | ECG BPM 사용 |
| Invalid | Valid | PPG BPM 사용 |
| Invalid | Invalid | 0 |

---

### 5.6 Temperature Measurement

TMP102 온도 센서는 I2C 주소 `0x48`로 읽습니다.

```cpp
#define TMP102_ADDR 0x48
```

```cpp
bodyTemp = readTemperatureTMP102();
```

---

### 5.7 Baseline Measurement

시스템 시작 후 일정 시간이 지나면 ECG, PPG, 온도 데이터를 수집하여 baseline을 계산합니다.

| Parameter | Value | Description |
|---|---:|---|
| `BASELINE_START_MS` | 20000 ms | 시스템 시작 후 baseline 수집 시작 시간 |
| `BASELINE_END_MS` | 50000 ms | baseline 계산 완료 시간 |
| `MAX_SAMPLES` | 600 | baseline 수집 최대 샘플 개수 |
| `SENSOR_INTERVAL_MS` | 100 ms | 센서 데이터 읽기 주기 |

계산되는 주요 baseline 값은 다음과 같습니다.

- `ecgMean`
- `ppgMean`
- `tempMean`
- `combinedBpmBase`
- `combinedThreshold`

Flutter 앱으로는 현재 사용 중인 값만 전송합니다.

```text
BASE_TEMP
BASE_COMB_BPM
```

---

### 5.8 Danger Detection Logic

baseline 계산이 완료된 뒤, 현재 통합 BPM이 기준 임계값보다 낮으면 위험 상태로 판단합니다.

```cpp
bool dangerCondition = combinedBpm > 0.0f && combinedBpm < combinedThreshold;
```

위험 상태 지속 시간에 따라 `WARNING`과 `ACTIVE` 상태가 결정됩니다.

| Duration | State | BLE Value | Action |
|---|---|---|---|
| Less than 6 s | Normal | `WARNING:NO`, `ACTIVE:NO` | 모터 정지 |
| 6 s ~ 10 s | Warning | `WARNING:YES`, `ACTIVE:NO` | 앱 경고 팝업 |
| 10 s or more | Active | `WARNING:NO`, `ACTIVE:YES` | 모터 구동 및 앱 위험 팝업 |

```cpp
const unsigned long WARNING_DELAY_MS = 6000UL;
const unsigned long ACTIVE_DELAY_MS = 10000UL;
```

---

### 5.9 Motor Control

위험 상태가 10초 이상 지속되어 `activeFlag`가 `true`가 되면 모터 또는 리니어 액추에이터가 확장 방향으로 동작합니다.

```cpp
if (activeFlag) {
  motorExtend(220);
} else {
  motorStop();
}
```

모터 핀 설정은 다음과 같습니다.

```cpp
#define ENA_PIN 9
#define IN1_PIN 7
#define IN2_PIN 8
```

---

### 5.10 BLE Data Transmission

BLE 장치 이름은 다음과 같습니다.

```cpp
#define BLE_DEVICE_NAME "LifeVest_UNIFIED"
```

```cpp
#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"
```

> 현재는 Flutter 앱과의 호환성을 위해 표준 Heart Rate Service UUID를 사용합니다. 

---

## 6. BLE Packet Format

BLE Notify로 전송되는 데이터는 쉼표로 구분된 텍스트 패킷입니다. Flutter 앱에서는 쉼표와 콜론을 기준으로 문자열을 파싱합니다.

### 6.1 Packet Format

```text
LAT:<latitude>,LON:<longitude>,COMB_BPM:<combined_bpm>,TEMP:<temperature>,PredictedDirection:<direction_angle>,ACTIVE:<YES/NO>,WARNING:<YES/NO>,BASE_TEMP:<temperature_baseline>,BASE_COMB_BPM:<combined_bpm_baseline>,MADEANGLE:<direction_string>,PPG_BPM:<ppg_bpm>
```

### 6.2 Example Packet

```text
LAT:36.981200,LON:126.019300,COMB_BPM:72.4,TEMP:36.50,PredictedDirection:85.23,ACTIVE:NO,WARNING:NO,BASE_TEMP:36.45,BASE_COMB_BPM:77.7,MADEANGLE:E,PPG_BPM:71.2
```

### 6.3 Flutter Mapping

| BLE Key | Flutter Meaning |
|---|---|
| `LAT` | 구명조끼 GPS 위도 |
| `LON` | 구명조끼 GPS 경도 |
| `COMB_BPM` | ECG/PPG 통합 심박수 |
| `TEMP` | TMP102 온도값 |
| `PredictedDirection` | IMU 기반 예측 이동 방향 각도 |
| `ACTIVE` | 자동 팽창 모터 작동 상태 |
| `WARNING` | 위험 경고 상태 |
| `BASE_TEMP` | baseline 온도 |
| `BASE_COMB_BPM` | baseline 통합 심박수 |
| `MADEANGLE` | 방향 문자열 |
| `PPG_BPM` | PPG 기반 심박수 |

---

## 7. Operation Flow

```text
시스템 시작
   ↓
Serial, Wire, GPS, IMU, PPG, Motor, BLE 초기화
   ↓
GPS 데이터 수집
   ↓
ECG / PPG / 온도 / IMU 데이터 읽기
   ↓
20초 후 baseline 수집 시작
   ↓
50초 후 baseline 계산 완료
   ↓
현재 통합 BPM과 baseline 임계값 비교
   ↓
위험 상태가 6초 이상 지속되면 WARNING = YES
   ↓
위험 상태가 10초 이상 지속되면 ACTIVE = YES
   ↓
ACTIVE 상태에서 모터 또는 리니어 액추에이터 작동
   ↓
BLE Notify를 통해 Flutter 앱으로 상태 데이터 전송
```

---

## 8. Important Code Parameters

| Parameter | Value | Meaning |
|---|---:|---|
| `GPS_CHECK_INTERVAL_MS` | 2000 ms | GPS 확인 주기 |
| `GPS_LOSS_THRESHOLD` | 10 | GPS 손실 판단 기준 횟수 |
| `DR_DURATION_MS` | 60000 ms | IMU 기반 Dead Reckoning 수행 시간 |
| `SENSOR_INTERVAL_MS` | 100 ms | ECG, PPG, 온도, IMU 데이터 읽기 주기 |
| `BLE_NOTIFY_INTERVAL_MS` | 1000 ms | BLE Notify 전송 주기 |
| `BASELINE_START_MS` | 20000 ms | baseline 수집 시작 시간 |
| `BASELINE_END_MS` | 50000 ms | baseline 계산 완료 시간 |
| `MAX_SAMPLES` | 600 | baseline 수집 최대 샘플 개수 |
| `ECG_WEIGHT` | 0.7 | 통합 BPM 계산 시 ECG 가중치 |
| `PPG_WEIGHT` | 0.3 | 통합 BPM 계산 시 PPG 가중치 |
| `PPG_IR_FINGER_THRESHOLD` | 15000 | PPG 손가락/접촉 감지 기준값 |
| `ECG_DYNAMIC_THRESHOLD_DEFAULT` | 10 | ECG 동적 peak 검출 기준값 |
| `PPG_AC_THRESH` | 200 | PPG AC peak 검출 기준값 |
| `WARNING_DELAY_MS` | 6000 ms | Warning 상태 전환 기준 시간 |
| `ACTIVE_DELAY_MS` | 10000 ms | Active 상태 전환 기준 시간 |
| `motorExtend(220)` | 220 | 모터 PWM 구동 속도 |

---

## 9. Future Improvements

- BLE 커스텀 서비스 UUID 적용
- BLE 패킷을 JSON 또는 바이너리 포맷으로 개선
- 모터 작동 후 자동 정지 조건 추가
- GPS fix 상태와 센서 오류 상태 BLE 전송
- 앱에서 위험 상태 로그 저장
