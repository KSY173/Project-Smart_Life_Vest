# 🦺Smart Life Vest: 자동 팽창 구명조끼

<p>
  <img src="https://img.shields.io/badge/LANGUAGE-C%2FC%2B%2B-00599C?style=for-the-badge&logo=cplusplus&logoColor=white"/>
  <img src="https://img.shields.io/badge/LANGUAGE-Dart-0175C2?style=for-the-badge&logo=dart&logoColor=white"/>
  <img src="https://img.shields.io/badge/FRAMEWORK-Flutter-02569B?style=for-the-badge&logo=flutter&logoColor=white"/>
  <img src="https://img.shields.io/badge/COMMUNICATION-BLE-0082FC?style=for-the-badge&logo=bluetooth&logoColor=white"/>
</p>

<p>
  <img src="https://img.shields.io/badge/MCU-Arduino%20Nano%20ESP32-00979D?style=for-the-badge&logo=arduino&logoColor=white"/>
  <img src="https://img.shields.io/badge/SENSOR-AD8232%20ECG-FF6B6B?style=for-the-badge"/>
  <img src="https://img.shields.io/badge/SENSOR-MAX30102%20PPG-FF8C42?style=for-the-badge"/>
  <img src="https://img.shields.io/badge/SENSOR-TMP102%20Temperature-F4B400?style=for-the-badge"/>
</p>

<p>
  <img src="https://img.shields.io/badge/SENSOR-ICM--20948%20IMU-34A853?style=for-the-badge"/>
  <img src="https://img.shields.io/badge/SENSOR-GNSS%20Module-00ACC1?style=for-the-badge"/>
  <img src="https://img.shields.io/badge/MOTOR%20DRIVER-L298N-8E44AD?style=for-the-badge"/>
  <img src="https://img.shields.io/badge/ACTUATOR-Linear%20Actuator-6A1B9A?style=for-the-badge"/>
</p>

본 프로젝트는 잠수 인력 및 수상 활동자의 안전을 높이기 위해 개발한 **자동 팽창 스마트 구명조끼 시스템**입니다.  
구명조끼에 부착된 센서와 제어부가 사용자의 생체 신호, 체온, 위치 정보, 이동 방향 정보를 수집하고, 위험 상태가 지속될 경우 자동으로 팽창 모듈을 구동합니다. 동시에 BLE를 통해 스마트폰 어플리케이션으로 사용자 상태를 전송하여 외부 감독자가 실시간으로 모니터링할 수 있도록 구성했습니다.

> 본 루트 README는 전체 시스템 개요를 설명합니다.  
> 아두이노 펌웨어와 Flutter 앱의 세부 구현, 실행 방법, 패킷 형식은 각 하위 README를 참고합니다.

---

## 1. Project Overview

* 2025 SMU(숙명여대) 캡스톤 경진대회 우수상 수상

### Background

기존 구명조끼는 크게 상시 팽창형, 수동 레버 작동형, 물 접촉 자동 팽창형으로 구분됩니다. 그러나 잠수 작업 환경에서는 다음과 같은 한계가 있습니다.

- 상시 팽창형은 잠수 작업을 방해할 수 있음
- 수동 레버 방식은 사용자가 의식이 없을 경우 작동 불가
- 물 접촉 자동 팽창 방식은 잠수 작업 자체와 충돌 가능
- 수중 사고는 외부에서 즉시 인지하기 어려움

본 프로젝트는 이러한 문제를 해결하기 위해 **생체 신호 기반 위험 판단**, **위치 및 이동 방향 제공**, **자동 팽창**, **스마트폰 모니터링**을 하나의 시스템으로 통합하는 것을 목표로 했습니다.

### Goal

본 시스템의 목표는 다음과 같습니다.

- ECG, PPG 기반 심박수 측정
- 개인별 baseline 기반 위험 상태 판단
- 체온, GNSS 위치, IMU 기반 방향 정보 제공
- WARNING / ACTIVE 상태 분류
- ACTIVE 상태 발생 시 팽창 모듈 자동 구동
- BLE 기반 스마트폰 앱 연동
- 앱에서 생체 신호, 위치, 이동 방향, 위험 알림 확인

---

## 2. System Architecture

전체 시스템은 크게 **센서부**, **제어부**, **팽창 모듈**, **스마트폰 어플리케이션**으로 구성됩니다.

```text
[Sensor Unit]
ECG / PPG / Temperature / IMU / GNSS
        ↓
[Control Unit]
Arduino Nano ESP32
- Sensor data acquisition
- Combined BPM calculation
- Baseline measurement
- Danger state decision
- BLE notification
        ↓
[Smartphone Application]
Flutter App
- BLE connection
- Sensor monitoring
- Graph visualization
- GPS location display
- WARNING / ACTIVE popup
        ↓
[Inflation Module]
L298N Motor Driver + Linear Actuator
- CO₂ cartridge trigger mechanism
```

---

## 3. Main Functions

### 3.1 Bio-Signal Monitoring

사용자의 생체 상태를 확인하기 위해 ECG와 PPG 센서를 함께 사용했습니다.

- ECG: 심장 전기 신호 기반 BPM 측정
- PPG: 광학식 맥박 변화 기반 BPM 측정
- Combined BPM: ECG와 PPG 값을 결합한 통합 심박수
- Temperature: 저체온 위험 확인을 위한 체온 정보 제공

ECG와 PPG는 각각 장단점이 있기 때문에, 두 값을 함께 사용하여 심박수 판단의 안정성을 높였습니다.

---

### 3.2 Baseline-Based Danger Detection

심박수는 사람마다 정상 범위가 다르기 때문에, 시스템 시작 후 일정 시간 동안 사용자의 기준선 데이터를 수집합니다.

이후 현재 Combined BPM이 baseline 기반 임계값보다 낮은 상태가 지속되는지 확인하여 위험 상태를 판단합니다.

| State | Meaning | Action |
|---|---|---|
| NORMAL | 정상 상태 | 센서 데이터 모니터링 |
| WARNING | 위험 가능 상태 | 앱에 경고 상태 표시 |
| ACTIVE | 자동 팽창 필요 상태 | 앱 팝업 표시 및 팽창 모듈 구동 |

순간적인 센서 노이즈로 인한 오판단을 줄이기 위해, 단일 측정값이 아니라 **위험 상태가 일정 시간 이상 지속되는지**를 함께 고려합니다.

---

### 3.3 Location and Direction Tracking

GNSS 모듈을 통해 사용자의 위도와 경도를 수집합니다.  
수중 환경에서는 GNSS 신호가 약해지거나 끊길 수 있으므로, 마지막으로 수신된 유효 위치를 기준 위치로 활용합니다.

GNSS 신호가 손실된 이후에는 IMU 데이터를 기반으로 사용자의 초기 이동 방향을 추정하고, 이를 방위 문자열 형태로 앱에 표시합니다.

예시:

```text
사용자의 이동방향 예측 : N
사용자의 이동방향 예측 : NE
사용자의 이동방향 예측 : E
```

---

### 3.4 BLE-Based Monitoring

Arduino Nano ESP32는 BLE Peripheral로 동작하며, Flutter 앱은 BLE Central로 동작합니다.

BLE Notify를 통해 다음과 같은 정보를 앱으로 전송합니다.

- 위치 정보: 위도, 경도
- 생체 정보: Combined BPM, PPG BPM, 체온
- 기준선 정보: baseline Combined BPM, baseline temperature
- 이동 방향: 예측 각도, 방향 문자열
- 위험 상태: WARNING, ACTIVE

상세 BLE 패킷 형식과 파싱 로직은 Arduino / Flutter 하위 README에 정리되어 있습니다.

---

### 3.5 Auto-Inflation Module

위험 상태가 ACTIVE로 판단되면 제어부는 모터 드라이버를 통해 리니어 액추에이터를 구동합니다.  
리니어 액추에이터는 구명조끼의 CO₂ 카트리지 작동 줄을 당기도록 구성되어 있으며, 이를 통해 사용자가 직접 조작하지 않아도 구명조끼가 팽창할 수 있도록 설계했습니다.

이 구조는 기존 수동 팽창 구명조끼의 작동 원리를 유지하면서 전기적 제어를 추가한 프로토타입 방식입니다.

---

## 4. Hardware Components

| Component | Role |
|---|---|
| Arduino Nano ESP32 | 센서 데이터 수집, 위험 판단, BLE 통신, 모터 제어 |
| AD8232 ECG Sensor | ECG 기반 심박수 측정 |
| MAX30102 PPG Sensor | PPG 기반 심박수 측정 |
| TMP102 Temperature Sensor | 체온 정보 측정 |
| ICM-20948 IMU | 움직임 및 방향 추정 |
| GNSS Module | 위치 정보 수집 |
| L298N Motor Driver | 리니어 액추에이터 제어 |
| Linear Actuator | CO₂ 카트리지 작동 줄 구동 |
| CO₂ Cartridge Life Vest | 팽창 대상 구명조끼 |
| Flutter Smartphone App | 실시간 상태 모니터링 |

세부 핀 구성과 센서 초기화 방식은 `arduino/README.md`를 참고합니다.

---

## 5. Software Components

### Arduino Firmware

아두이노 펌웨어는 센서 데이터 수집, baseline 계산, 위험 상태 판단, 모터 제어, BLE 데이터 전송을 담당합니다.

자세한 내용은 다음 파일을 참고합니다.

```text
hw_code/README.md
```

### Flutter Application

Flutter 앱은 BLE 장치 검색 및 연결, 센서 데이터 수신, 그래프 표시, 사용자 위치 표시, WARNING / ACTIVE 팝업 표시를 담당합니다.

자세한 내용은 다음 파일을 참고합니다.

```text
sw_code/README.md
```

---

## 6. Application Screens

Flutter 앱은 다음 기능 화면으로 구성됩니다.

| Screen | Function |
|---|---|
| Home | BLE 연결 상태, baseline 결과, 장치 스캔 목록 표시 |
| Real-Time Status | Combined BPM 및 체온 그래프 표시 |
| User Location | BLE로 수신한 GNSS 위치와 이동 방향 표시 |
| Real-Time Heart Rate | PPG 기반 실시간 심박수 표시 |
| Warning / Active Popup | 위험 상태 발생 시 팝업 알림 표시 |

---

## 7. Operation Flow

```text
1. 사용자가 스마트 구명조끼 착용
2. Arduino Nano ESP32 및 센서 초기화
3. Flutter 앱에서 BLE 장치 검색 후 연결
4. 착용 초기 baseline 데이터 수집
5. ECG / PPG / 온도 / GNSS / IMU 데이터 수집
6. Combined BPM 계산
7. baseline 기반 위험 상태 판단
8. NORMAL / WARNING / ACTIVE 상태 결정
9. BLE Notify로 앱에 상태 정보 전송
10. ACTIVE 상태 발생 시 팽창 모듈 구동
11. 앱에서 위치, 방향, 생체 신호, 위험 알림 확인
```

---

## 8. Research Result Summary

본 프로젝트에서는 일반 구명조끼에 센서부, 제어부, BLE 통신부, 팽창 모듈을 통합하여 자동 팽창 스마트 구명조끼 프로토타입을 구현했습니다.

구현 결과는 다음과 같습니다.

- ECG / PPG 기반 Combined BPM 측정
- 사용자 baseline 기반 위험 판단
- WARNING 및 ACTIVE 상태 구분
- ACTIVE 상태 발생 시 리니어 액추에이터 기반 팽창 동작 구현
- BLE 기반 Flutter 앱 연동
- 앱에서 생체 신호 그래프, baseline 결과, 사용자 위치, 이동 방향 표시
- 위험 상태 발생 시 앱 팝업 알림 표시

이를 통해 단순 모니터링을 넘어, 위험 상태 판단과 자동 팽창 동작을 하나의 시스템으로 연결할 수 있음을 확인했습니다.

---

## 9. Limitations

본 프로젝트는 프로토타입 단계이며, 실제 수중 작업 환경에 적용하기 위해서는 다음 개선이 필요합니다.

### 9.1 BLE Communication Range

BLE는 근거리 통신 방식이므로 통신 가능 거리에 제한이 있습니다.  
특히 수중 환경에서는 무선 신호가 급격히 약해질 수 있으므로, 실제 운용에서는 스마트폰 또는 수신 장치가 수면 위에 있거나 구명조끼와 가까운 위치에 있어야 합니다.

장거리 구조 요청이 필요한 환경에서는 LTE, LoRa, 위성 통신 등과의 연동이 필요합니다.

### 9.2 Underwater Location Accuracy

GNSS는 수중에서 안정적으로 동작하기 어렵기 때문에, 본 시스템은 마지막 GNSS 수신 위치와 IMU 기반 방향 추정 정보를 함께 제공합니다.  
다만 IMU 기반 방향 추정은 누적 오차, 자세 변화, 물살 등에 영향을 받을 수 있으므로 향후 보정 알고리즘이 필요합니다.

### 9.3 Inflation Module Size and Weight

CO₂ 카트리지 작동부를 당기기 위해 충분한 힘과 스트로크를 가진 리니어 액추에이터가 필요했습니다.  
이로 인해 팽창 모듈의 크기와 무게가 증가할 수 있으므로, 실제 착용성을 높이기 위해 경량화가 필요합니다.

### 9.4 Waterproofing and Durability

현재 프로토타입은 지상 환경에서 구조와 동작을 검증한 단계입니다.  
실제 수중 환경 적용을 위해서는 수압을 견딜 수 있는 방수 케이스, 방수 커넥터, 절연 처리, 센서 고정 구조, 모터부 내구성 검증이 필요합니다.

### 9.5 Power Stability

센서 측정, BLE 통신, 모터 구동이 모두 전원에 의존합니다.  
특히 모터는 순간적으로 큰 전류를 필요로 하므로, 제어부 전원과 모터 전원을 안정적으로 분리하고 배터리 잔량 모니터링 기능을 추가할 필요가 있습니다.

---

## 10. Future Improvements

- 방수 구조 및 방수 커넥터 적용
- 팽창 모듈 소형화 및 경량화
- 배터리 잔량 표시 및 저전압 경고 기능 추가
- BLE 외 장거리 통신 방식 연동
- IMU 기반 이동 방향 추정 정확도 개선
- 센서 접촉 불량 감지 기능 추가
- 팽창 전 최종 안전 확인 또는 수동 해제 장치 추가
- 실제 수중 환경 테스트 및 내구성 검증
