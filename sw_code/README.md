## 2. Main Features

### 2.1 BLE Scan and Connection

앱은 `flutter_blue_plus`를 사용하여 BLE 장치를 스캔하고 연결합니다.

BLE 스캔은 아래 Service UUID를 기준으로 수행됩니다.

```dart
const String serviceUuid = '0000180d-0000-1000-8000-00805f9b34fb';
const String characteristicUuid = '00002a37-0000-1000-8000-00805f9b34fb';
```

연결 후 Characteristic Notify를 활성화하여 아두이노 또는 ESP32 기반 구명조끼 장치에서 전송하는 센서 데이터를 수신합니다.

<p align="center">
  <img src="./imgs/app_bluetooth.jpg" width="320"><br>
  <b>블루투스 연결 전 화면</b>
</p>

---

### 2.2 BLE Data Parsing

BLE Notify 데이터는 쉼표(`,`)와 콜론(`:`)으로 구분된 문자열 형태입니다.

앱은 다음 키 값을 파싱합니다.

| BLE Key              | Flutter 변수                      | 설명                   |
| -------------------- | ------------------------------- | -------------------- |
| `LAT`                | `latitude`                      | BLE 장치에서 수신한 위도      |
| `LON`                | `longitude`                     | BLE 장치에서 수신한 경도      |
| `COMB_BPM`           | `combinedBpm`                   | ECG와 PPG를 결합한 통합 심박수 |
| `PPG_BPM`            | `ppgBpm`                        | PPG 기반 실시간 심박수       |
| `TEMP`               | `bodyTemperature`               | 온도 센서값               |
| `PredictedDirection` | `predictedDirection`            | 이동 방향 예측 각도          |
| `MADEANGLE`          | `madeAngle`                     | 이동 방향 문자열            |
| `WARNING`            | `warningFlag`                   | 경고 상태 여부             |
| `ACTIVE`             | `activeFlag`                    | 자동 팽창 또는 모터 구동 상태 여부 |
| `BASE_COMB_BPM`      | `baselineCombinedBpmMeanResult` | 기초 통합 BPM            |
| `BASE_TEMP`          | `baselineBodyTemperatureResult` | 기초 체온                |

`PredictedDirection`은 코드 내부에서 대소문자와 관계없이 처리되도록 `key.toUpperCase()` 방식으로 파싱합니다.

---

### 2.3 Real-Time Status Graph

하단 메뉴의 **실시간 상태** 탭에서는 다음 두 가지 그래프를 표시합니다.

* `COMB_BPM` 추이
* 체온 추이

센서 기록은 약 5초 간격으로 `sensorHistory`에 저장되며, `fl_chart`를 사용하여 라인 차트로 시각화합니다.

```dart
final List<SensorData> sensorHistory = [];
```

<p align="center">
  <img src="./imgs/app_graph.jpg" width="320"><br>
  <b>실시간 센서값 그래프 화면</b>
</p>

---

### 2.4 Warning and Active Popup

BLE 데이터에서 `WARNING:YES` 또는 `ACTIVE:YES`가 수신되면 앱 전체 화면 위에 위험 팝업이 표시됩니다.

| 상태                        | 조건             | 앱 표시          |
| ------------------------- | -------------- | ------------- |
| `WARNING:YES`             | 위험 상태 발생       | WARNING 상태 팝업 |
| `ACTIVE:YES`              | 자동 팽창 또는 모터 작동 | ACTIVE 상태 팝업  |
| `WARNING:NO`, `ACTIVE:NO` | 정상 상태          | 팝업 자동 해제      |

팝업은 사용자가 닫을 수 있으며, 사용자가 닫은 직후에는 5초 동안 다시 표시되지 않도록 쿨다운 로직이 적용되어 있습니다.

```dart
void closePopup() {
  _showPopup = false;
  _isManuallyClosed = true;
  ...
}
```

<p align="center">
  <img src="./imgs/app_warning.jpg" width="320">
  <img src="./imgs/app_active.jpg" width="320"><br>
  <b>WARNING / ACTIVE 상태 팝업 화면</b>
</p>

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

<p align="center">
  <img src="./imgs/app_map.jpg" width="320"><br>
  <b>BLE GPS 기반 사용자 위치 표시 화면</b>
</p>

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

<p align="center">
  <img src="./imgs/app_bpm_game.jpg" width="320"><br>
  <b>실시간 PPG 심박수 측정 화면</b>
</p>

---

### 2.7 Baseline Measurement Result

BLE 데이터에서 `BASE_COMB_BPM`과 `BASE_TEMP`가 수신되면 홈 화면에 측정 완료 카드가 표시됩니다.

표시 항목은 다음과 같습니다.

* 기초 통합 BPM
* 기초 체온

```dart
baselineCombinedBpmMeanResult
baselineBodyTemperatureResult
```

<p align="center">
  <img src="./imgs/app_baseline.jpg" width="320"><br>
  <b>Baseline 측정 완료 후 홈 화면</b>
</p>
