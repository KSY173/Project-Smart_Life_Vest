# Smart Life Vest - Arduino Firmware

This project is the embedded firmware for an automatic inflatable smart life vest.
It integrates data from GPS, IMU, ECG, PPG, and temperature sensors to determine the user's location, movement direction, biometric signals, and risk status. The system also transmits status information to a Flutter app through BLE Notify.

---

## 1. Project Overview

The Smart Life Vest Arduino firmware performs the following functions:

* Collects latitude and longitude data using GPS
* Estimates movement direction using IMU-based Dead Reckoning when GPS signal is lost
* Measures heart rate using the AD8232 ECG sensor
* Measures heart rate using the MAX30102/MAX30105 PPG sensor
* Measures temperature using the TMP102 temperature sensor
* Calculates a combined BPM using both ECG and PPG data
* Determines risk status based on baseline measurements
* Sends `WARNING` and `ACTIVE` status through BLE
* Drives the inflation mechanism using an L298N motor driver when the `ACTIVE` state is triggered
* Transmits BLE text packets compatible with a Flutter application

---

## 2. Hardware Components

| Component               | Purpose                                                                        |
| ----------------------- | ------------------------------------------------------------------------------ |
| Arduino Nano ESP32      | Main control board                                                             |
| GNSS / GPS Module       | Tracks the user's location                                                     |
| ICM-20948 IMU           | Estimates posture and movement direction using acceleration and gyroscope data |
| MAX30102 / MAX30105     | Measures heart rate based on PPG signals                                       |
| AD8232                  | Measures heart rate based on ECG signals                                       |
| TMP102                  | Measures temperature                                                           |
| L298N Motor Driver      | Controls the motor or linear actuator                                          |
| Motor / Linear Actuator | Drives the life vest inflation mechanism                                       |
| BLE                     | Transmits status data to the Flutter app                                       |

---

## 3. Pin Configuration

| Pin         | Connected Module                     | Description                   |
| ----------- | ------------------------------------ | ----------------------------- |
| `A0`        | AD8232 ECG                           | ECG analog signal input       |
| `D2`        | AD8232 LO+                           | ECG lead-off detection input  |
| `D3`        | AD8232 LO-                           | ECG lead-off detection input  |
| `D7`        | L298N IN1                            | Motor direction control       |
| `D8`        | L298N IN2                            | Motor direction control       |
| `D9`        | L298N ENA                            | Motor PWM speed control       |
| `D16`       | GPS RX                               | GPS Serial1 RX                |
| `D17`       | GPS TX                               | GPS Serial1 TX                |
| I2C SDA/SCL | ICM-20948, MAX30102/MAX30105, TMP102 | I2C communication for sensors |

```cpp
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3

#define GPS_RX_PIN    16
#define GPS_TX_PIN    17
```

---

## 4. Software Requirements

The following libraries are required when using Arduino IDE or PlatformIO:

* `TinyGPSPlus`
* `ICM_20948`
* `MAX30105`
* `Wire`
* `BLEDevice`
* `BLEServer`
* `BLEUtils`
* `BLE2902`

---

## 5. System Features

### 5.1 GPS Location Tracking

The system receives latitude and longitude data from the GPS module.
GPS data is parsed using the `TinyGPSPlus` library.

```cpp
TinyGPSPlus gps;
```

The GPS location is checked every 2 seconds.

```cpp
const unsigned long GPS_CHECK_INTERVAL_MS = 2000UL;
```

When a valid GPS location is updated, it is stored in `lastGpsLat` and `lastGpsLon`, and transmitted through the BLE packet as `LAT` and `LON` values.

---

### 5.2 GPS Loss Detection and Dead Reckoning

After GPS data has been successfully received at least once, the system determines that the GPS signal is lost if location updates fail continuously.

```cpp
const uint8_t GPS_LOSS_THRESHOLD = 10;
```

When GPS loss is detected, the system enters IMU-based Dead Reckoning mode.

```cpp
const unsigned long DR_DURATION_MS = 60000UL;
```

In Dead Reckoning mode, acceleration and gyroscope data from the ICM-20948 are collected for approximately 60 seconds to estimate the user's movement direction.
The estimated result is converted into an angle and a direction string.

Example:

```text
PredictedDirection:85.23,MADEANGLE:E
```

---

### 5.3 ECG Heart Rate Measurement

The system reads the analog signal from the AD8232 ECG sensor and calculates BPM based on R-peak detection.

```cpp
#define ECG_PIN       A0
#define LO_PLUS_PIN   2
#define LO_MINUS_PIN  3
```

If ECG lead-off is detected, the ECG value is treated as invalid.

```cpp
if (digitalRead(LO_PLUS_PIN) == HIGH || digitalRead(LO_MINUS_PIN) == HIGH) {
  return 0;
}
```

---

### 5.4 PPG Heart Rate Measurement

The system detects PPG peaks and calculates BPM based on the IR value from the MAX30102/MAX30105 sensor.

```cpp
MAX30105 ppg;
```

Finger contact with the PPG sensor is determined using an IR threshold.

```cpp
#define PPG_IR_FINGER_THRESHOLD 15000UL
```

---

### 5.5 Combined BPM Calculation

When both ECG and PPG values are valid, the system calculates a combined BPM by applying a 70% weight to ECG and a 30% weight to PPG.

```cpp
const float ECG_WEIGHT = 0.7f;
const float PPG_WEIGHT = 0.3f;
```

| ECG     | PPG     | Combined BPM            |
| ------- | ------- | ----------------------- |
| Valid   | Valid   | `0.7 * ECG + 0.3 * PPG` |
| Valid   | Invalid | Uses ECG BPM            |
| Invalid | Valid   | Uses PPG BPM            |
| Invalid | Invalid | 0                       |

---

### 5.6 Temperature Measurement

The TMP102 temperature sensor is read through the I2C address `0x48`.

```cpp
#define TMP102_ADDR 0x48
```

```cpp
bodyTemp = readTemperatureTMP102();
```

---

### 5.7 Baseline Measurement

After the system starts, ECG, PPG, and temperature data are collected for a certain period to calculate baseline values.

| Parameter            |    Value | Description                                               |
| -------------------- | -------: | --------------------------------------------------------- |
| `BASELINE_START_MS`  | 20000 ms | Time when baseline collection starts after system startup |
| `BASELINE_END_MS`    | 50000 ms | Time when baseline calculation is completed               |
| `MAX_SAMPLES`        |      600 | Maximum number of samples for baseline collection         |
| `SENSOR_INTERVAL_MS` |   100 ms | Sensor data reading interval                              |

The main calculated baseline values are:

* `ecgMean`
* `ppgMean`
* `tempMean`
* `combinedBpmBase`
* `combinedThreshold`

Only the values currently used by the Flutter app are transmitted.

```text
BASE_TEMP
BASE_COMB_BPM
```

---

### 5.8 Danger Detection Logic

After baseline calculation is completed, the system determines a dangerous state when the current combined BPM is lower than the baseline threshold.

```cpp
bool dangerCondition = combinedBpm > 0.0f && combinedBpm < combinedThreshold;
```

Depending on how long the dangerous state continues, the system switches between `WARNING` and `ACTIVE` states.

| Duration      | State   | BLE Value                  | Action                                       |
| ------------- | ------- | -------------------------- | -------------------------------------------- |
| Less than 6 s | Normal  | `WARNING:NO`, `ACTIVE:NO`  | Motor stopped                                |
| 6 s ~ 10 s    | Warning | `WARNING:YES`, `ACTIVE:NO` | Warning popup in the app                     |
| 10 s or more  | Active  | `WARNING:NO`, `ACTIVE:YES` | Motor activation and danger popup in the app |

```cpp
const unsigned long WARNING_DELAY_MS = 6000UL;
const unsigned long ACTIVE_DELAY_MS = 10000UL;
```

---

### 5.9 Motor Control

When the dangerous state continues for more than 10 seconds and `activeFlag` becomes `true`, the motor or linear actuator operates in the extension direction.

```cpp
if (activeFlag) {
  motorExtend(220);
} else {
  motorStop();
}
```

The motor pins are configured as follows:

```cpp
#define ENA_PIN 9
#define IN1_PIN 7
#define IN2_PIN 8
```

---

### 5.10 BLE Data Transmission

The BLE device name is defined as follows:

```cpp
#define BLE_DEVICE_NAME "LifeVest_UNIFIED"
```

```cpp
#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"
```

> The standard Heart Rate Service UUID is currently used for compatibility with the Flutter application.

---

## 6. BLE Packet Format

The data transmitted through BLE Notify is a comma-separated text packet.
The Flutter app parses the packet string based on commas and colons.

### 6.1 Packet Format

```text
LAT:<latitude>,LON:<longitude>,COMB_BPM:<combined_bpm>,TEMP:<temperature>,PredictedDirection:<direction_angle>,ACTIVE:<YES/NO>,WARNING:<YES/NO>,BASE_TEMP:<temperature_baseline>,BASE_COMB_BPM:<combined_bpm_baseline>,MADEANGLE:<direction_string>,PPG_BPM:<ppg_bpm>
```

### 6.2 Example Packet

```text
LAT:36.981200,LON:126.019300,COMB_BPM:72.4,TEMP:36.50,PredictedDirection:85.23,ACTIVE:NO,WARNING:NO,BASE_TEMP:36.45,BASE_COMB_BPM:77.7,MADEANGLE:E,PPG_BPM:71.2
```

### 6.3 Flutter Mapping

| BLE Key              | Flutter Meaning                              |
| -------------------- | -------------------------------------------- |
| `LAT`                | GPS latitude of the life vest                |
| `LON`                | GPS longitude of the life vest               |
| `COMB_BPM`           | Combined heart rate based on ECG and PPG     |
| `TEMP`               | Temperature value measured by TMP102         |
| `PredictedDirection` | IMU-based predicted movement direction angle |
| `ACTIVE`             | Automatic inflation motor activation status  |
| `WARNING`            | Danger warning status                        |
| `BASE_TEMP`          | Baseline temperature                         |
| `BASE_COMB_BPM`      | Baseline combined heart rate                 |
| `MADEANGLE`          | Direction string                             |
| `PPG_BPM`            | Heart rate based on PPG                      |

---

## 7. Operation Flow

```text
System start
   ↓
Initialize Serial, Wire, GPS, IMU, PPG, Motor, and BLE
   ↓
Collect GPS data
   ↓
Read ECG / PPG / temperature / IMU data
   ↓
Start baseline collection after 20 seconds
   ↓
Complete baseline calculation after 50 seconds
   ↓
Compare the current combined BPM with the baseline threshold
   ↓
If the dangerous state continues for more than 6 seconds, set WARNING = YES
   ↓
If the dangerous state continues for more than 10 seconds, set ACTIVE = YES
   ↓
Operate the motor or linear actuator in the ACTIVE state
   ↓
Transmit status data to the Flutter app through BLE Notify
```

---

## 8. Important Code Parameters

| Parameter                       |    Value | Meaning                                              |
| ------------------------------- | -------: | ---------------------------------------------------- |
| `GPS_CHECK_INTERVAL_MS`         |  2000 ms | GPS check interval                                   |
| `GPS_LOSS_THRESHOLD`            |       10 | Threshold count for GPS loss detection               |
| `DR_DURATION_MS`                | 60000 ms | Duration of IMU-based Dead Reckoning                 |
| `SENSOR_INTERVAL_MS`            |   100 ms | ECG, PPG, temperature, and IMU data reading interval |
| `BLE_NOTIFY_INTERVAL_MS`        |  1000 ms | BLE Notify transmission interval                     |
| `BASELINE_START_MS`             | 20000 ms | Baseline collection start time                       |
| `BASELINE_END_MS`               | 50000 ms | Baseline calculation completion time                 |
| `MAX_SAMPLES`                   |      600 | Maximum number of samples for baseline collection    |
| `ECG_WEIGHT`                    |      0.7 | ECG weight for combined BPM calculation              |
| `PPG_WEIGHT`                    |      0.3 | PPG weight for combined BPM calculation              |
| `PPG_IR_FINGER_THRESHOLD`       |    15000 | Threshold for PPG finger/contact detection           |
| `ECG_DYNAMIC_THRESHOLD_DEFAULT` |       10 | Default dynamic threshold for ECG peak detection     |
| `PPG_AC_THRESH`                 |      200 | Threshold for PPG AC peak detection                  |
| `WARNING_DELAY_MS`              |  6000 ms | Time threshold for switching to the Warning state    |
| `ACTIVE_DELAY_MS`               | 10000 ms | Time threshold for switching to the Active state     |
| `motorExtend(220)`              |      220 | Motor PWM driving speed                              |

---

## 9. Future Improvements

* Apply a custom BLE service UUID
* Improve the BLE packet format using JSON or binary encoding
* Add an automatic stop condition after motor activation
* Transmit GPS fix status and sensor error status through BLE
* Store danger state logs in the mobile app
