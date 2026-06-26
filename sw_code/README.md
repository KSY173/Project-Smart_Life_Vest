# Smart Life Vest - Flutter App

[English README](./README.eng.md)

본 Flutter 앱은 자동팽창식 스마트 구명조끼에서 BLE Notify로 전송되는 센서 데이터를 수신하고, 사용자의 생체 신호, 위험 상태, 위치, 이동 방향 정보를 시각화하는 모바일 애플리케이션입니다.

앱은 BLE 장치 스캔 및 연결, COMB_BPM/체온 그래프 표시, PPG 기반 실시간 심박수 표시, BLE GPS 위치 지도 표시, WARNING/ACTIVE 위험 팝업 알림 기능을 제공합니다.

---

## 1. Project Overview

### 1.1 주요 목적

- BLE 기반 스마트 구명조끼 센서 데이터 수신
- 심박수, 체온, 위치, 방향, 위험 상태 데이터 파싱
- 사용자 상태를 앱 화면에서 실시간 모니터링
- 위험 상태 발생 시 팝업 알림 표시
- BLE로 수신한 GPS 좌표를 Google Map에 표시

### 1.2 앱 이름

```text
자동팽창식 스마트 구명조끼
```

---

## 2. Main Features

### 2.1 BLE Scan and Connection

앱은 `flutter_blue_plus`를 사용하여 BLE 장치를 스캔하고 연결합니다.

BLE 스캔은 아래 Service UUID를 기준으로 수행됩니다.

```dart
const String serviceUuid = '0000180d-0000-1000-8000-00805f9b34fb';
const String characteristicUuid = '00002a37-0000-1000-8000-00805f9b34fb';
```

연결 후 Characteristic Notify를 활성화하여 아두이노 또는 ESP32 기반 구명조끼 장치에서 전송하는 센서 데이터를 수신합니다.

---

### 2.2 BLE Data Parsing

BLE Notify 데이터는 쉼표(`,`)와 콜론(`:`)으로 구분된 문자열 형태입니다.

앱은 다음 키 값을 파싱합니다.

| BLE Key | Flutter 변수 | 설명 |
|---|---|---|
| `LAT` | `latitude` | BLE 장치에서 수신한 위도 |
| `LON` | `longitude` | BLE 장치에서 수신한 경도 |
| `COMB_BPM` | `combinedBpm` | ECG와 PPG를 결합한 통합 심박수 |
| `PPG_BPM` | `ppgBpm` | PPG 기반 실시간 심박수 |
| `TEMP` | `bodyTemperature` | 온도 센서값 |
| `PredictedDirection` | `predictedDirection` | 이동 방향 예측 각도 |
| `MADEANGLE` | `madeAngle` | 이동 방향 문자열 |
| `WARNING` | `warningFlag` | 경고 상태 여부 |
| `ACTIVE` | `activeFlag` | 자동 팽창 또는 모터 구동 상태 여부 |
| `BASE_COMB_BPM` | `baselineCombinedBpmMeanResult` | 기초 통합 BPM |
| `BASE_TEMP` | `baselineBodyTemperatureResult` | 기초 체온 |

`PredictedDirection`은 코드 내부에서 대소문자와 관계없이 처리되도록 `key.toUpperCase()` 방식으로 파싱합니다.

---

### 2.3 Real-Time Status Graph

하단 메뉴의 **실시간 상태** 탭에서는 다음 두 가지 그래프를 표시합니다.

- `COMB_BPM` 추이
- 체온 추이

센서 기록은 약 5초 간격으로 `sensorHistory`에 저장되며, `fl_chart`를 사용하여 라인 차트로 시각화합니다.

```dart
final List<SensorData> sensorHistory = [];
```

---

### 2.4 Warning and Active Popup

BLE 데이터에서 `WARNING:YES` 또는 `ACTIVE:YES`가 수신되면 앱 전체 화면 위에 위험 팝업이 표시됩니다.

| 상태 | 조건 | 앱 표시 |
|---|---|---|
| `WARNING:YES` | 위험 상태 발생 | WARNING 상태 팝업 |
| `ACTIVE:YES` | 자동 팽창 또는 모터 작동 | ACTIVE 상태 팝업 |
| `WARNING:NO`, `ACTIVE:NO` | 정상 상태 | 팝업 자동 해제 |

팝업은 사용자가 닫을 수 있으며, 사용자가 닫은 직후에는 5초 동안 다시 표시되지 않도록 쿨다운 로직이 적용되어 있습니다.

```dart
void closePopup() {
  _showPopup = false;
  _isManuallyClosed = true;
  ...
}
```

---

### 2.5 BLE GPS Location Map

하단 메뉴의 **사용자 위치** 탭에서는 BLE로 수신한 `LAT`, `LON` 값을 Google Map에 표시합니다.

이 앱은 스마트폰 자체 GPS가 아니라, BLE 장치에서 전송하는 구명조끼 GPS 좌표를 지도에 표시합니다.

BLE GPS 데이터가 아직 수신되지 않은 경우에는 다음 문구를 표시합니다.

```text
📡 BLE GPS 데이터를 기다리는 중...
```

BLE GPS 데이터가 수신되면 지도 마커가 아래 ID로 생성됩니다.

```dart
MarkerId('lifevest_ble_location')
```

또한 `PredictedDirection` 값은 마커 회전값으로 사용되며, `MADEANGLE` 값은 지도 상단에 텍스트로 표시됩니다.

---

### 2.6 Real-Time Heart Rate Screen

하단 메뉴의 **실시간 심박수** 탭에서는 `PPG_BPM` 값을 크게 표시합니다.

```dart
final ppgBpm = context.watch<BleService>().ppgBpm;
```

표시 형식은 다음과 같습니다.

```text
72.4 BPM
```

---

### 2.7 Baseline Measurement Result

BLE 데이터에서 `BASE_COMB_BPM`과 `BASE_TEMP`가 수신되면 홈 화면에 측정 완료 카드가 표시됩니다.

표시 항목은 다음과 같습니다.

- 기초 통합 BPM
- 기초 체온

```dart
baselineCombinedBpmMeanResult
baselineBodyTemperatureResult
```

---

## 3. App Screen Structure

앱은 하단 Bottom Navigation Bar를 통해 4개의 화면으로 구성됩니다.

| 탭 | 화면 클래스 | 기능 |
|---|---|---|
| 실시간 상태 | `UserStatusScreen` | COMB_BPM, 체온 그래프 표시 |
| 홈화면 | `HomeScreen` | BLE 연결 상태, 장치 스캔, 위험 상태, baseline 결과 표시 |
| 사용자 위치 | `UserLocationScreen` | BLE GPS 위치와 방향 정보 지도 표시 |
| 실시간 심박수 | `HeartRateScreen` | PPG BPM 단독 표시 |

---

## 4. BLE Packet Format

아두이노 또는 ESP32 장치는 다음 형식의 BLE Notify 문자열을 전송해야 합니다.

```text
LAT:<latitude>,LON:<longitude>,COMB_BPM:<combined_bpm>,TEMP:<temperature>,PredictedDirection:<direction_angle>,ACTIVE:<YES/NO>,WARNING:<YES/NO>,BASE_TEMP:<baseline_temperature>,BASE_COMB_BPM:<baseline_combined_bpm>,MADEANGLE:<direction_string>,PPG_BPM:<ppg_bpm>
```

예시:

```text
LAT:36.981200,LON:126.019300,COMB_BPM:72.4,TEMP:33.20,PredictedDirection:85.23,ACTIVE:NO,WARNING:NO,BASE_TEMP:33.1,BASE_COMB_BPM:77.7,MADEANGLE:E,PPG_BPM:71.2
```

### 4.1 필수 데이터

앱의 주요 기능을 모두 사용하려면 다음 값들이 필요합니다.

| 기능 | 필요한 BLE Key |
|---|---|
| BLE 위치 지도 | `LAT`, `LON` |
| 방향 표시 | `PredictedDirection`, `MADEANGLE` |
| 실시간 상태 그래프 | `COMB_BPM`, `TEMP` |
| 실시간 심박수 화면 | `PPG_BPM` |
| 위험 팝업 | `WARNING`, `ACTIVE` |
| 기초 정보 표시 | `BASE_COMB_BPM`, `BASE_TEMP` |

---

## 5. Required Packages

`main.dart`에서 사용하는 주요 Flutter 패키지는 다음과 같습니다.

```yaml
dependencies:
  flutter:
    sdk: flutter
  provider: ^6.1.2
  permission_handler: ^11.3.1
  flutter_blue_plus: ^1.25.0
  google_maps_flutter: ^2.7.0
  fl_chart: ^0.68.0
```

프로젝트에 따라 버전은 조정될 수 있습니다.

---

## 6. Operation Flow

```text
앱 실행
   ↓
위치 권한 요청
   ↓
Provider를 통해 BleService 초기화
   ↓
홈 화면에서 BLE 장치 스캔
   ↓
LifeVest BLE 장치 선택 및 연결
   ↓
Service / Characteristic 탐색
   ↓
Notify 활성화
   ↓
BLE 문자열 데이터 수신
   ↓
쉼표와 콜론 기준으로 데이터 파싱
   ↓
COMB_BPM, TEMP, PPG_BPM, LAT, LON, WARNING, ACTIVE 등 상태 업데이트
   ↓
그래프 / 지도 / 심박수 / 위험 팝업 UI 갱신
```

---

## 7. Future Improvements

- BLE 커스텀 Service UUID 적용
- BLE 데이터 포맷을 JSON 또는 바이너리 구조로 개선
- 데이터 수신 로그 저장 기능 추가
- 위험 상태 발생 이력 저장
- GPS 수신 끊김 상태 UI 표시
- 지도에 이동 경로 polyline 표시
- ECG/PPG 개별 그래프 추가
- 앱 화면을 파일별로 분리하여 구조 개선
- 다국어 지원 추가
