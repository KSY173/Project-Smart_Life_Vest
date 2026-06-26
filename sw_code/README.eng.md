# Smart Life Vest - Flutter App

This Flutter app is a mobile application that receives sensor data transmitted through BLE Notify from an automatic inflatable smart life vest.
It visualizes the user's biometric signals, risk status, location, and movement direction.

The app provides BLE device scanning and connection, COMB_BPM/body temperature graph visualization, real-time PPG-based heart rate display, BLE GPS location display on Google Maps, and WARNING/ACTIVE risk popup notifications.

---

## 1. Project Overview

### 1.1 Main Objectives

* Receive smart life vest sensor data through BLE
* Parse heart rate, body temperature, location, direction, and risk status data
* Monitor the user's condition in real time on the app screen
* Display popup alerts when a dangerous state occurs
* Display GPS coordinates received through BLE on Google Maps

### 1.2 App Name

```text
Automatic Inflatable Smart Life Vest
```

---

## 2. Main Features

### 2.1 BLE Scan and Connection

The app scans and connects to BLE devices using `flutter_blue_plus`.

BLE scanning is performed based on the following Service UUID.

```dart
const String serviceUuid = '0000180d-0000-1000-8000-00805f9b34fb';
const String characteristicUuid = '00002a37-0000-1000-8000-00805f9b34fb';
```

After connection, the app enables Characteristic Notify and receives sensor data transmitted from the Arduino or ESP32-based life vest device.

---

### 2.2 BLE Data Parsing

BLE Notify data is transmitted as a string separated by commas `,` and colons `:`.

The app parses the following key values.

| BLE Key              | Flutter Variable                | Description                                     |
| -------------------- | ------------------------------- | ----------------------------------------------- |
| `LAT`                | `latitude`                      | Latitude received from the BLE device           |
| `LON`                | `longitude`                     | Longitude received from the BLE device          |
| `COMB_BPM`           | `combinedBpm`                   | Combined heart rate calculated from ECG and PPG |
| `PPG_BPM`            | `ppgBpm`                        | Real-time heart rate based on PPG               |
| `TEMP`               | `bodyTemperature`               | Temperature sensor value                        |
| `PredictedDirection` | `predictedDirection`            | Predicted movement direction angle              |
| `MADEANGLE`          | `madeAngle`                     | Movement direction string                       |
| `WARNING`            | `warningFlag`                   | Warning status                                  |
| `ACTIVE`             | `activeFlag`                    | Automatic inflation or motor activation status  |
| `BASE_COMB_BPM`      | `baselineCombinedBpmMeanResult` | Baseline combined BPM                           |
| `BASE_TEMP`          | `baselineBodyTemperatureResult` | Baseline body temperature                       |

`PredictedDirection` is parsed using `key.toUpperCase()` so that it can be handled regardless of letter case.

---

### 2.3 Real-Time Status Graph

The **Real-Time Status** tab in the bottom navigation menu displays the following two graphs.

* `COMB_BPM` trend
* Body temperature trend

Sensor records are stored in `sensorHistory` at approximately 5-second intervals and visualized as line charts using `fl_chart`.

```dart
final List<SensorData> sensorHistory = [];
```

---

### 2.4 Warning and Active Popup

When `WARNING:YES` or `ACTIVE:YES` is received from BLE data, a risk popup is displayed over the entire app screen.

| State                     | Condition                              | App Display                   |
| ------------------------- | -------------------------------------- | ----------------------------- |
| `WARNING:YES`             | Risk condition detected                | WARNING status popup          |
| `ACTIVE:YES`              | Automatic inflation or motor operation | ACTIVE status popup           |
| `WARNING:NO`, `ACTIVE:NO` | Normal state                           | Popup automatically dismissed |

The popup can be closed by the user.
After the user manually closes it, a cooldown logic prevents the popup from being displayed again for 5 seconds.

```dart
void closePopup() {
  _showPopup = false;
  _isManuallyClosed = true;
  ...
}
```

---

### 2.5 BLE GPS Location Map

The **User Location** tab in the bottom navigation menu displays the `LAT` and `LON` values received through BLE on Google Maps.

This app does not use the smartphone's GPS.
Instead, it displays the GPS coordinates transmitted from the smart life vest device through BLE.

If BLE GPS data has not been received yet, the following message is displayed.

```text
📡 Waiting for BLE GPS data...
```

When BLE GPS data is received, a map marker is created with the following ID.

```dart
MarkerId('lifevest_ble_location')
```

The `PredictedDirection` value is also used as the marker rotation value, and the `MADEANGLE` value is displayed as text at the top of the map.

---

### 2.6 Real-Time Heart Rate Screen

The **Real-Time Heart Rate** tab in the bottom navigation menu displays the `PPG_BPM` value in a large format.

```dart
final ppgBpm = context.watch<BleService>().ppgBpm;
```

The display format is as follows.

```text
72.4 BPM
```

---

### 2.7 Baseline Measurement Result

When `BASE_COMB_BPM` and `BASE_TEMP` are received from BLE data, a measurement completion card is displayed on the home screen.

The displayed values are:

* Baseline combined BPM
* Baseline body temperature

```dart
baselineCombinedBpmMeanResult
baselineBodyTemperatureResult
```

---

## 3. App Screen Structure

The app consists of four screens accessible through the Bottom Navigation Bar.

| Tab                  | Screen Class         | Function                                                                       |
| -------------------- | -------------------- | ------------------------------------------------------------------------------ |
| Real-Time Status     | `UserStatusScreen`   | Displays COMB_BPM and body temperature graphs                                  |
| Home                 | `HomeScreen`         | Displays BLE connection status, device scan, risk status, and baseline results |
| User Location        | `UserLocationScreen` | Displays BLE GPS location and direction information on a map                   |
| Real-Time Heart Rate | `HeartRateScreen`    | Displays PPG BPM only                                                          |

---

## 4. BLE Packet Format

The Arduino or ESP32 device must transmit a BLE Notify string in the following format.

```text
LAT:<latitude>,LON:<longitude>,COMB_BPM:<combined_bpm>,TEMP:<temperature>,PredictedDirection:<direction_angle>,ACTIVE:<YES/NO>,WARNING:<YES/NO>,BASE_TEMP:<baseline_temperature>,BASE_COMB_BPM:<baseline_combined_bpm>,MADEANGLE:<direction_string>,PPG_BPM:<ppg_bpm>
```

Example:

```text
LAT:36.981200,LON:126.019300,COMB_BPM:72.4,TEMP:33.20,PredictedDirection:85.23,ACTIVE:NO,WARNING:NO,BASE_TEMP:33.1,BASE_COMB_BPM:77.7,MADEANGLE:E,PPG_BPM:71.2
```

### 4.1 Required Data

To use all major app features, the following BLE keys are required.

| Feature                      | Required BLE Key                  |
| ---------------------------- | --------------------------------- |
| BLE location map             | `LAT`, `LON`                      |
| Direction display            | `PredictedDirection`, `MADEANGLE` |
| Real-time status graph       | `COMB_BPM`, `TEMP`                |
| Real-time heart rate screen  | `PPG_BPM`                         |
| Risk popup                   | `WARNING`, `ACTIVE`               |
| Baseline information display | `BASE_COMB_BPM`, `BASE_TEMP`      |

---

## 5. Required Packages

The main Flutter packages used in `main.dart` are as follows.

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

Package versions may be adjusted depending on the project environment.

---

## 6. Operation Flow

```text
Launch app
   ↓
Request location permission
   ↓
Initialize BleService through Provider
   ↓
Scan BLE devices on the home screen
   ↓
Select and connect to the LifeVest BLE device
   ↓
Discover Service / Characteristic
   ↓
Enable Notify
   ↓
Receive BLE string data
   ↓
Parse data based on commas and colons
   ↓
Update states such as COMB_BPM, TEMP, PPG_BPM, LAT, LON, WARNING, and ACTIVE
   ↓
Update graph / map / heart rate / risk popup UI
```

---

## 7. Future Improvements

* Apply a custom BLE Service UUID
* Improve the BLE data format using JSON or binary encoding
* Add a data reception log feature
* Store risk state history
* Display GPS disconnection status in the UI
* Display movement path using a polyline on the map
* Add separate ECG and PPG graphs
* Improve the app structure by separating screens into individual files
* Add multilingual support
