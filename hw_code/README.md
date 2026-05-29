# Smart Life Vest - Arduino Code

본 프로젝트는 GPS, IMU, ECG, PPG, 온도 센서 데이터를 통합하여 사용자의 위치, 방향, 생체 신호, 자세 변화를 수집하고 BLE를 통해 앱 또는 외부 장치로 상태 정보를 전송합니다.

---

## 1. System Features

### 1.1 GPS Location Tracking

GPS 모듈에서 위도와 경도 데이터를 수신합니다.  
코드에서는 `TinyGPSPlus` 라이브러리를 사용하여 GPS 데이터를 파싱합니다.

```cpp
TinyGPSPlus gps;
```

GPS 데이터는 2초마다 확인되며, 유효한 위치 정보가 있으면 `last_gps_lat`, `last_gps_lon`에 저장됩니다.

```cpp
const unsigned long GPS_PRINT_INTERVAL = 2000UL;
```

---

### 1.2 GPS Loss Detection and Dead Reckoning

GPS 위치 데이터 확인 횟수가 `GPS_LOSS_THRESHOLD` 이상이 되면 GPS 신호 손실 상태로 판단하고 DR 모드에 진입합니다.

```cpp
const int GPS_LOSS_THRESHOLD = 10;
```

DR 모드에서는 ICM-20948 IMU의 가속도 및 자이로 데이터를 이용하여 약 60초 동안 방향을 추정합니다.

```cpp
const unsigned long DR_DURATION_MS = 60UL * 1000UL;
```

추정된 방향은 각도 값과 방향 문자열로 변환되어 BLE로 전송됩니다.

예시 전송 데이터:

```text
LAT:36.981200,LON:126.019300,PredictedDirection:85.23,MADEANGLE:E
```

---

### 1.3 ECG and PPG Heart Rate Measurement

ECG 센서는 AD8232 모듈을 기준으로 작성되어 있으며, 아날로그 입력값을 이용해 R-peak를 검출하고 BPM을 계산합니다.

```cpp
#define ECG_PIN A0
#define LO_PLUS_PIN 2
#define LO_MINUS_PIN 3
```

PPG 센서는 MAX30102 계열 센서를 사용하며, IR 값을 기반으로 피크를 검출하고 BPM을 계산합니다.

```cpp
MAX30105 ppg;
```

최종 심박수는 ECG와 PPG를 가중 평균하여 계산합니다.

```cpp
const float wECG = 0.7f;
const float wPPG = 0.3f;
```

즉, ECG 데이터에 70%, PPG 데이터에 30%의 가중치를 두어 `combinedBPM`을 계산합니다.

---

### 1.4 Temperature Measurement

TMP102 온도 센서를 I2C 주소 `0x48`로 읽도록 작성되어 있습니다.

```cpp
#define TMP102_ADDR 0x48
```

다만 현재 코드에서는 실제 TMP102 측정값을 읽은 뒤, 아래의 시뮬레이션 온도값으로 다시 덮어씁니다.

```cpp
static float simTemp = 33.0f;
tempC = simTemp;
```

따라서 현재 BLE로 전송되는 온도값은 실제 TMP102 측정값이 아니라 시뮬레이션된 온도값입니다.

---

### 1.5 Baseline Measurement

시스템 시작 후 일정 시간이 지나면 ECG, PPG, 온도 데이터를 수집하여 baseline을 계산합니다.

현재 코드 기준 baseline 수집은 다음과 같이 동작합니다.

- 시스템 시작 후 20초가 지나면 baseline 수집 시작
- 시스템 시작 후 50초가 지나면 baseline 계산 완료
- 최대 샘플 수는 600개
- 센서 데이터는 100ms 간격으로 읽음

```cpp
#define MAX_SAMPLES 600
const unsigned long sensorIntervalMs = 100;
```

baseline 계산 결과는 다음 값으로 저장됩니다.

- `ecgMean`
- `ppgMean`
- `tempMean`
- `combinedBPMBase`
- `combinedThr`

---

### 1.6 Danger Detection Logic

baseline 계산이 끝난 뒤, 현재 통합 심박수 `combinedBPM`이 기준 임계값 `combinedThr`보다 낮으면 위험 상태로 판단합니다.

```cpp
if (combinedBPM > 0 && combinedBPM < combinedThr)
```

위험 상태 지속 시간에 따라 상태가 구분됩니다.

| 지속 시간 | 상태 | 동작 |
|---|---|---|
| 6초 이상 10초 미만 | Warning | 경고 상태 활성화 |
| 10초 이상 | Active | 모터 작동 |

즉, 심박 이상 상태가 6초 이상 지속되면 경고 상태가 되고, 10초 이상 지속되면 자동 팽창 모터가 작동합니다.

---

### 1.7 Motor Control

모터 제어는 L298N 모터 드라이버 또는 리니어 액추에이터 구동을 기준으로 작성되어 있습니다.

```cpp
#define ENA_PIN 9
#define IN1_PIN 7
#define IN2_PIN 8
```

위험 상태가 10초 이상 지속되어 `active`가 `true`가 되면 모터가 확장 방향으로 작동합니다.

```cpp
if (active) motorExtend(220);
else motorStop();
```

---

### 1.8 BLE Data Transmission

BLE 장치 이름은 다음과 같이 설정되어 있습니다.

```cpp
BLEDevice::init("LifeVest_UNIFIED");
```

BLE UUID는 Heart Rate Service UUID를 사용하고 있습니다.

```cpp
#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"
```

BLE Notify로 전송되는 데이터 형식은 다음과 같습니다.

```text
LAT:위도,
LON:경도,
COMB_BPM:통합심박수,
TEMP:온도,
predictedDirection:예측방향각도,
ACTIVE:모터작동상태,
WARNING:경고상태,
BASE_ECG:ECG기준값,
BASE_PPG:PPG기준값,
BASE_TEMP:온도기준값,
BASE_COMB_BPM:통합심박기준값,
MADEANGLE:방향문자열,
PPG_BPM:PPG심박수
```

예시:

```text
LAT:36.981200,LON:126.019300,COMB_BPM:72.4,TEMP:33.20,predictedDirection:85.23,ACTIVE:NO,WARNING:NO,BASE_ECG:80.7,BASE_PPG:70.8,BASE_TEMP:33.1,BASE_COMB_BPM:77.7,MADEANGLE:E,PPG_BPM:71.2
```

---

## 2. Hardware Components

| Component | 용도 |
|---|---|
| Arduino nano esp32 | 전체 시스템을 제어하는 메인 컨트롤러 |
| GNSS 모듈 | 현재 위치 정보 추적 |
| ICM-20948 IMU | 가속도, 각속도, 기울기, 이동 방향 추정 |
| MAX30102 PPG 센서 | PPG 신호 기반 심박수 측정 |
| AD8232 ECG 센서 | ECG 신호 기반 심박수 측정 |
| TMP102 온도 센서 | 체온 또는 주변 온도 측정 |
| L298N 모터 드라이버 | 모터 또는 리니어 액추에이터 구동 제어 |
| 모터 / 리니어 액추에이터 | 구명조끼 팽창 메커니즘 구동 |
| BLE 모 | 무선 데이터 전송 |

---

## 3. Pin Configuration

| Pin | Connected Module | 기능 |
|---|---|---|
| A0 | AD8232 ECG | ECG 아날로그 신호 입력 |
| D2 | AD8232 LO+ | ECG 리드오프 감지 입력 |
| D3 | AD8232 LO- | ECG 리드오프 감지 입력 |
| D7 | L298N IN1 | 모터 회전 방향 제어 |
| D8 | L298N IN2 | 모터 회전 방향 제어 |
| D9 | L298N ENA | 모터 PWM 속도 제어 |
| I2C SDA/SCL | ICM-20948, MAX30102, TMP102 | 센서 데이터 통신 |
| Serial1 RX/TX | GPS module | GPS 데이터 수신 |

---

## 4. Software Requirements

Arduino IDE에서 필요한 주요 라이브러리는 다음과 같습니다.

- `TinyGPSPlus`
- `ICM_20948`
- `MAX30105`
- `heartRate`
- `BLEDevice`
- `BLEServer`
- `BLEUtils`
- `BLE2902`
- `Wire`

---

## 5. Operation Flow

```text
시스템 시작
   ↓
Serial, Wire, GPS, IMU, PPG, Motor, BLE 초기화
   ↓
GPS 데이터 수집
   ↓
ECG / PPG / 온도 / IMU 기울기 데이터 읽기
   ↓
20초 후 baseline 수집 시작
   ↓
50초 후 baseline 값 계산
   ↓
현재 통합 BPM과 baseline 기준값 비교
   ↓
낮은 BPM 상태가 6초 동안 지속될 경우
   ↓
WARNING = YES 설정
   ↓
낮은 BPM 상태가 10초 동안 지속될 경우
   ↓
ACTIVE = YES 설정
   ↓
모터 / 액추에이터 작동
   ↓
BLE Notify를 통해 센서 및 상태 데이터 전송
```

---

## 6. BLE Packet Format

이 장치는 BLE Notify를 통해 쉼표로 구분된 텍스트 패킷을 주기적으로 전송합니다. 

```text
LAT:<latitude>,LON:<longitude>,COMB_BPM:<combined_bpm>,TEMP:<temperature>,predictedDirection:<direction_angle>,ACTIVE:<YES/NO>,WARNING:<YES/NO>,BASE_ECG:<ecg_baseline>,BASE_PPG:<ppg_baseline>,BASE_TEMP:<temp_baseline>,BASE_COMB_BPM:<combined_baseline>,MADEANGLE:<direction_string>,PPG_BPM:<ppg_bpm>
```

어플리케이션은 쉼표와 콜론을 기준으로 문자열을 분리하여 이 패킷을 쉽게 파싱할 수 있습니다. 

---

## 7. Important Code Parameters

| 파라미터 | 수치 값 | 동 |
|---|---:|---|
| `GPS_PRINT_INTERVAL` | 2000 ms | GPS 확인 주기 |
| `GPS_LOSS_THRESHOLD` | 10 | GPS 손실 판단 기준 횟수 |
| `DR_DURATION_MS` | 60000 ms | IMU 기반 Dead Reckoning 데이터 수집 시간 |
| `sensorIntervalMs` | 100 ms | ECG, PPG, 온도, 기울기 데이터 읽기 주기 |
| `MAX_SAMPLES` | 600 | baseline 수집 최대 샘플 개수 |
| `wECG` | 0.7 | 통합 BPM 계산 시 ECG 가중치 |
| `wPPG` | 0.3 | 통합 BPM 계산 시 PPG 가중치 |
| `PPG_IR_FINGER_THRESHOLD` | 15000 | PPG 손가락/접촉 감지 기준값 |
| `ECG_DYNAMIC_THRESHOLD_DEFAULT` | 10 | ECG 동적 피크 감지 기준값 |
| `PPG_AC_THRESH` | 200 | PPG AC 피크 감지 기준값 |
| `motorExtend` speed | 220 | 모터 PWM 구동 속도 |

---

## 8. Current Limitations and Notes

현재 코드 기준으로 확인이 필요하거나 수정하면 좋은 부분은 다음과 같습니다.

### 8.1 GPS 핀과 ECG 핀 충돌 가능성

코드에서는 ECG lead-off 핀으로 D2, D3을 사용합니다.

```cpp
#define LO_PLUS_PIN 2
#define LO_MINUS_PIN 3
```

그런데 GPS Serial1도 아래처럼 2번, 3번 핀을 사용하도록 설정되어 있습니다.

```cpp
Serial1.begin(9600, SERIAL_8N1, 2, 3);
```

ESP32 계열 보드에서는 `Serial1.begin(baud, config, rx, tx)` 형식이 가능하지만, Arduino Nano ESP32 또는 다른 보드에서는 실제 핀 매핑이 다를 수 있습니다.  
따라서 GPS 핀과 ECG lead-off 핀이 실제로 충돌하는지 확인해야 합니다.

---

### 8.2 온도값이 실제 센서값이 아니라 시뮬레이션 값으로 전송됨

`readTemperatureTMP102()`로 TMP102 값을 읽지만, 이후 `simTemp` 값으로 덮어씁니다.

```cpp
tempC = simTemp;
```

실제 온도 센서값을 사용하려면 이 부분을 제거해야 합니다.

---

### 8.3 GPS 손실 판단 로직 확인 필요

현재 코드는 GPS가 유효하든 유효하지 않든 `gpsCount++`가 증가합니다.  
따라서 GPS가 정상적으로 수신되는 상황에서도 일정 시간이 지나면 DR 모드로 진입할 수 있습니다.

실제 GPS 손실을 판단하려면 유효하지 않은 GPS 데이터가 연속으로 발생했을 때만 카운트를 증가시키는 방식으로 수정하는 것이 좋습니다.

---

### 8.4 방향 문자열 오타 가능성

방향 문자열 중 남동쪽이 일반적으로 `SE`인데, 현재 코드에서는 `ES`로 작성되어 있습니다.

```cpp
strcpy(direction, "ES");
```

일반적인 표기법을 사용하려면 `SE`로 수정하는 것이 좋습니다.

---

### 8.5 BLE UUID 확인 필요

현재 BLE UUID는 표준 Heart Rate Service UUID를 사용하고 있습니다.

```cpp
0000180d-0000-1000-8000-00805f9b34fb
```

하지만 실제 전송 데이터는 심박수뿐 아니라 GPS, 온도, 모터 상태, 방향 데이터까지 포함합니다.  
앱에서 직접 파싱하는 목적이라면 커스텀 UUID를 사용하는 것이 더 명확할 수 있습니다.

---

### 8.6 `snprintf` 인자 개수 확인 필요

DR 결과 전송 부분에서 포맷 문자열에 비해 전달 인자가 하나 더 들어가 있습니다.

```cpp
snprintf(drPacket, sizeof(drPacket),
         "LAT:%.6f,LON:%.6f,PredictedDirection:%.2f,MADEANGLE:%s",
         last_gps_lat, last_gps_lon, fused, direction, global_direction_str);
```

현재 동작에는 큰 문제가 없을 수 있지만, 불필요한 인자인 `global_direction_str`는 제거하는 것이 좋습니다.

---

### 8.7 `A7` 사용 가능 여부 확인 필요

코드에 다음 구문이 있습니다.

```cpp
randomSeed(analogRead(A7));
```

사용하는 보드에 따라 `A7` 핀이 없을 수 있습니다.  
보드에 `A7`이 없다면 컴파일 오류가 발생할 수 있으므로 다른 아날로그 핀으로 변경해야 합니다.

---

## 8. Future Improvements

- GPS 손실 판단 로직 개선
- 실제 TMP102 온도값 사용
- BLE 데이터 포맷을 JSON 형태로 변경
- BLE 커스텀 서비스 UUID 적용
- 모터 작동 후 자동 정지 조건 추가
- ECG lead-off 상태를 위험 판단 로직에 반영
- 센서별 오류 상태 BLE 전송
- 앱에서 `WARNING`, `ACTIVE`, `MADEANGLE` 상태 시각화
- 팽창 모듈 작동 로그 저장



