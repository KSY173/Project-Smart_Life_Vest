import 'dart:async';
import 'dart:math';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:google_maps_flutter/google_maps_flutter.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:provider/provider.dart';

const String serviceUuid = '0000180d-0000-1000-8000-00805f9b34fb';
const String characteristicUuid = '00002a37-0000-1000-8000-00805f9b34fb';

enum SensorType { combinedBpm, temperature }

class SensorData {
  final double timestamp;
  final double latitude;
  final double longitude;
  final double combinedBpm;
  final double bodyTemperature;
  final double predictedDirection;
  final bool active;
  final bool warning;

  const SensorData({
    required this.timestamp,
    required this.latitude,
    required this.longitude,
    required this.combinedBpm,
    required this.bodyTemperature,
    required this.predictedDirection,
    required this.active,
    required this.warning,
  });
}

class BleService extends ChangeNotifier {
  List<ScanResult> scanResults = [];
  BluetoothDevice? connectedDevice;

  StreamSubscription<List<ScanResult>>? _scanSubscription;
  StreamSubscription<List<int>>? _dataSubscription;
  StreamSubscription<BluetoothConnectionState>? _connectionSubscription;

  Timer? _uiUpdateTimer;
  Timer? _coolDownTimer;

  bool _isManuallyClosed = false;
  bool _showPopup = false;

  bool get showPopup => _showPopup;

  double? latitude;
  double? longitude;
  DateTime? lastGpsTime;

  double combinedBpm = 0.0;
  double ppgBpm = 0.0;
  double bodyTemperature = 0.0;

  bool warningFlag = false;
  bool activeFlag = false;

  double? _predictedDirection;
  double? get predictedDirection => _predictedDirection;

  String _madeAngle = '';
  String get madeAngle => _madeAngle;

  final List<SensorData> sensorHistory = [];
  double _lastGraphTimestamp = 0.0;

  bool _isMeasuringBaseline = false;
  bool get isMeasuringBaseline => _isMeasuringBaseline;

  int _measurementDuration = 0;
  int get measurementDuration => _measurementDuration;

  Timer? _baselineTimer;
  double? baselineCombinedBpmMeanResult;
  double? baselineBodyTemperatureResult;

  bool get gpsTimeout {
    return lastGpsTime == null ||
        DateTime.now().difference(lastGpsTime!) > const Duration(seconds: 10);
  }

  void closePopup() {
    _showPopup = false;
    _isManuallyClosed = true;

    _coolDownTimer?.cancel();
    _coolDownTimer = Timer(const Duration(seconds: 5), () {
      _isManuallyClosed = false;
      notifyListeners();
    });

    notifyListeners();
  }

  void _updateWarningStatus(bool newWarning, bool newActive) {
    if ((newWarning || newActive) && !_isManuallyClosed) {
      _showPopup = true;
    }

    if (!newWarning && !newActive) {
      _showPopup = false;
      _isManuallyClosed = false;
      _coolDownTimer?.cancel();
    }

    warningFlag = newWarning;
    activeFlag = newActive;
  }

  void _updateMadeAngle(String newMessage) {
    if (_madeAngle != newMessage) {
      _madeAngle = newMessage;
      notifyListeners();
    }
  }

  void _startUiUpdateTimer() {
    _uiUpdateTimer?.cancel();
    _uiUpdateTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      notifyListeners();
    });
  }

  Future<void> startScan() async {
    scanResults = [];
    notifyListeners();

    await _scanSubscription?.cancel();
    _scanSubscription = FlutterBluePlus.scanResults.listen((results) {
      for (final result in results) {
        final isAlreadyAdded = scanResults.any(
          (scanResult) => scanResult.device.remoteId == result.device.remoteId,
        );
        if (!isAlreadyAdded) {
          scanResults.add(result);
        }
      }
      notifyListeners();
    });

    await FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 4),
      withServices: [Guid(serviceUuid)],
    );
  }

  Future<void> stopScan() async {
    await FlutterBluePlus.stopScan();
  }

  Future<void> connectToDevice(BluetoothDevice device) async {
    await stopScan();

    try {
      await device.connect();

      try {
        await device.requestMtu(247);
      } catch (e) {
        debugPrint('MTU request skipped: $e');
      }

      connectedDevice = device;
      notifyListeners();

      await discoverServices(device);

      await _connectionSubscription?.cancel();
      _connectionSubscription = device.connectionState.listen((state) {
        if (state == BluetoothConnectionState.disconnected) {
          disconnectDevice();
        }
      });
    } catch (e) {
      debugPrint('Connection failed: $e');
      connectedDevice = null;
      notifyListeners();
    }
  }

  Future<void> discoverServices(BluetoothDevice device) async {
    final services = await device.discoverServices();

    for (final service in services) {
      if (service.uuid == Guid(serviceUuid)) {
        for (final characteristic in service.characteristics) {
          if (characteristic.uuid == Guid(characteristicUuid)) {
            await subscribeToCharacteristic(characteristic);
            return;
          }
        }
      }
    }
  }

  Future<void> subscribeToCharacteristic(
    BluetoothCharacteristic characteristic,
  ) async {
    await _dataSubscription?.cancel();
    await characteristic.setNotifyValue(true);
    _startUiUpdateTimer();

    _dataSubscription = characteristic.lastValueStream.listen((value) {
      if (value.isEmpty) return;

      try {
        final fullData = String.fromCharCodes(value).trim();
        final parts = fullData.split(',');

        bool newWarningFlag = warningFlag;
        bool newActiveFlag = activeFlag;
        double newCombinedBpm = combinedBpm;
        double newPpgBpm = ppgBpm;
        double newBodyTemperature = bodyTemperature;
        double? newLatitude;
        double? newLongitude;
        double? newDirection;
        String? newMadeAngle;

        for (final rawPart in parts) {
          final part = rawPart.trim();
          final separatorIndex = part.indexOf(':');
          if (separatorIndex == -1) continue;

          final key = part.substring(0, separatorIndex).trim();
          final valueString = part.substring(separatorIndex + 1).trim();
          final numericValue = double.tryParse(valueString);

          switch (key.toUpperCase()) {
            case 'COMB_BPM':
              newCombinedBpm = numericValue ?? combinedBpm;
              break;
            case 'PPG_BPM':
              newPpgBpm = numericValue ?? ppgBpm;
              break;
            case 'TEMP':
              newBodyTemperature = numericValue ?? bodyTemperature;
              break;
            case 'LAT':
              newLatitude = numericValue;
              break;
            case 'LON':
              newLongitude = numericValue;
              break;
            case 'WARNING':
              newWarningFlag = valueString.toUpperCase() == 'YES';
              break;
            case 'ACTIVE':
              newActiveFlag = valueString.toUpperCase() == 'YES';
              break;
            case 'PREDICTEDDIRECTION':
              newDirection = numericValue;
              break;
            case 'MADEANGLE':
              newMadeAngle = valueString;
              break;
            case 'BASE_COMB_BPM':
              baselineCombinedBpmMeanResult =
                  numericValue ?? baselineCombinedBpmMeanResult;
              break;
            case 'BASE_TEMP':
              baselineBodyTemperatureResult =
                  numericValue ?? baselineBodyTemperatureResult;
              break;
          }
        }

        if (newMadeAngle != null) {
          _updateMadeAngle(newMadeAngle);
        }

        if (newLatitude != null && newLongitude != null) {
          latitude = newLatitude;
          longitude = newLongitude;
          lastGpsTime = DateTime.now();
        }

        if (newDirection != null) {
          _predictedDirection = newDirection;
        }

        combinedBpm = newCombinedBpm;
        ppgBpm = newPpgBpm;
        bodyTemperature = newBodyTemperature;

        _updateWarningStatus(newWarningFlag, newActiveFlag);
        _recordSensorHistoryIfNeeded();

        notifyListeners();
      } catch (e) {
        debugPrint('Data parsing error: $e');
        debugPrint('Received data raw bytes: $value');
      }
    });
  }

  void _recordSensorHistoryIfNeeded() {
    final nowTime = DateTime.now().millisecondsSinceEpoch / 1000.0;
    final shouldRecord =
        _lastGraphTimestamp == 0 || nowTime >= _lastGraphTimestamp + 5;

    if (!shouldRecord || combinedBpm <= 0) return;

    _lastGraphTimestamp = nowTime;

    sensorHistory.add(
      SensorData(
        timestamp: _lastGraphTimestamp,
        latitude: latitude ?? 0.0,
        longitude: longitude ?? 0.0,
        combinedBpm: combinedBpm,
        bodyTemperature: bodyTemperature,
        predictedDirection: _predictedDirection ?? 0.0,
        active: activeFlag,
        warning: warningFlag,
      ),
    );
  }

  Future<void> disconnectDevice() async {
    await _dataSubscription?.cancel();
    _uiUpdateTimer?.cancel();
    _baselineTimer?.cancel();

    try {
      await connectedDevice?.disconnect();
    } catch (e) {
      debugPrint('Disconnect failed: $e');
    }

    connectedDevice = null;
    combinedBpm = 0.0;
    ppgBpm = 0.0;
    bodyTemperature = 0.0;
    warningFlag = false;
    activeFlag = false;
    sensorHistory.clear();
    _lastGraphTimestamp = 0.0;
    _showPopup = false;
    _isManuallyClosed = false;
    _isMeasuringBaseline = false;
    _measurementDuration = 0;
    baselineCombinedBpmMeanResult = null;
    baselineBodyTemperatureResult = null;
    latitude = null;
    longitude = null;
    lastGpsTime = null;
    _predictedDirection = null;
    _madeAngle = '';

    notifyListeners();
  }

  @override
  void dispose() {
    _scanSubscription?.cancel();
    _dataSubscription?.cancel();
    _connectionSubscription?.cancel();
    _uiUpdateTimer?.cancel();
    _baselineTimer?.cancel();
    _coolDownTimer?.cancel();
    super.dispose();
  }
}

Future<void> requestBlePermissions(BuildContext context) async {
  if (Theme.of(context).platform == TargetPlatform.android) {
    await Permission.bluetoothScan.request();
    await Permission.bluetoothConnect.request();
  }
}

Future<void> requestLocationPermissions() async {
  await Permission.locationWhenInUse.request();
}

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await requestLocationPermissions();

  runApp(
    ChangeNotifierProvider(
      create: (_) => BleService(),
      child: const MyApp(),
    ),
  );
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  int _selectedIndex = 1;

  final List<Widget> _screens = const <Widget>[
    UserStatusScreen(),
    HomeScreen(),
    UserLocationScreen(),
    HeartRateScreen(),
  ];

  void _onItemTapped(int index) {
    setState(() {
      _selectedIndex = index;
    });
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Scaffold(
        appBar: AppBar(
          centerTitle: true,
          title: const Text(
            '자동팽창식 스마트 구명조끼',
            style: TextStyle(fontWeight: FontWeight.bold, fontSize: 18),
          ),
          backgroundColor: Colors.blue[300],
        ),
        body: Stack(
          children: [
            Center(child: _screens.elementAt(_selectedIndex)),
            const WarningPopupOverlay(),
          ],
        ),
        bottomNavigationBar: BottomNavigationBar(
          type: BottomNavigationBarType.fixed,
          currentIndex: _selectedIndex,
          selectedItemColor: Colors.blueAccent,
          onTap: _onItemTapped,
          items: const <BottomNavigationBarItem>[
            BottomNavigationBarItem(
              icon: Icon(Icons.monitor_heart),
              label: '실시간 상태',
            ),
            BottomNavigationBarItem(
              icon: Icon(Icons.home_filled),
              label: '홈화면',
            ),
            BottomNavigationBarItem(
              icon: Icon(Icons.location_on),
              label: '사용자 위치',
            ),
            BottomNavigationBarItem(
              icon: Icon(Icons.favorite_border),
              label: '실시간 심박수',
            ),
          ],
        ),
      ),
    );
  }
}

class WarningPopupOverlay extends StatelessWidget {
  const WarningPopupOverlay({super.key});

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();

    if (!bleService.showPopup) {
      return const SizedBox.shrink();
    }

    final isActive = bleService.activeFlag;

    return Container(
      color: Colors.black45,
      child: Center(
        child: Container(
          margin: const EdgeInsets.symmetric(horizontal: 30),
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: isActive ? Colors.red[50] : Colors.orange[50],
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: Colors.red, width: 2),
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Align(
                alignment: Alignment.topRight,
                child: GestureDetector(
                  onTap: bleService.closePopup,
                  child: const Icon(Icons.cancel, color: Colors.grey, size: 30),
                ),
              ),
              const Icon(Icons.warning, color: Colors.red, size: 50),
              const SizedBox(height: 10),
              Text(
                isActive ? 'ACTIVE 상태 발생!' : 'WARNING 상태 발생!',
                style: const TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.bold,
                  color: Colors.red,
                ),
              ),
              const SizedBox(height: 10),
              Text(
                isActive
                    ? '구명조끼가 팽창되었습니다.\n사용자의 위치를 확인하십시오.'
                    : '사용자의 심박수 또는 체온이 위험 수준입니다.',
                textAlign: TextAlign.center,
                style: const TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w500,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

Widget buildRoundedContainer({
  required Widget child,
  required Color color,
  double height = 0,
  EdgeInsetsGeometry margin =
      const EdgeInsets.symmetric(horizontal: 8.0, vertical: 4.0),
  double borderRadius = 12.0,
  int flex = 0,
}) {
  final container = Container(
    height: flex == 0 ? height : double.infinity,
    margin: margin,
    decoration: BoxDecoration(
      color: color,
      borderRadius: BorderRadius.circular(borderRadius),
      boxShadow: [
        BoxShadow(
          color: Colors.grey.withOpacity(0.1),
          spreadRadius: 1,
          blurRadius: 3,
          offset: const Offset(0, 1),
        ),
      ],
    ),
    child: child,
  );

  if (flex > 0) {
    return Expanded(flex: flex, child: container);
  }
  return container;
}

class SensorChart extends StatelessWidget {
  final List<SensorData> data;
  final String title;
  final SensorType type;
  final double intervalY;

  const SensorChart({
    super.key,
    required this.data,
    required this.title,
    required this.type,
    this.intervalY = 5,
  });

  double _getValue(SensorData data) {
    switch (type) {
      case SensorType.combinedBpm:
        return data.combinedBpm;
      case SensorType.temperature:
        return data.bodyTemperature;
    }
  }

  Color _getColor() {
    switch (type) {
      case SensorType.combinedBpm:
        return const Color(0xFFA7AAE1);
      case SensorType.temperature:
        return const Color(0xFFF2AEBB);
    }
  }

  String _getUnit() {
    return type == SensorType.temperature ? '°C' : 'BPM';
  }

  bool get _isTemperatureChart => type == SensorType.temperature;

  @override
  Widget build(BuildContext context) {
    final validData = data.where((item) => _getValue(item) > 0).toList();

    if (validData.isEmpty) {
      return Center(
        child: Text(
          '$title 기록 시작',
          style: const TextStyle(color: Colors.grey),
        ),
      );
    }

    final minX = validData.first.timestamp;
    final maxX = validData.last.timestamp;
    final values = validData.map(_getValue).toList();
    final maxValue = values.reduce(max);
    final minValue = values.reduce(min);

    double minY = _isTemperatureChart ? max(30.0, minValue * 0.95) : 36.0;
    double maxY = _isTemperatureChart ? maxValue * 1.05 : max(maxValue * 1.1, 100.0);

    minY = (minY / intervalY).floor() * intervalY;
    maxY = (maxY / intervalY).ceil() * intervalY;

    final lastData = validData.last;
    final lastValue = _getValue(lastData);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.only(left: 12, top: 8, right: 12),
          child: Text(
            title,
            style: const TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.bold,
              color: Colors.blueGrey,
            ),
          ),
        ),
        Expanded(
          child: Padding(
            padding: const EdgeInsets.only(
              top: 10,
              right: 20,
              left: 10,
              bottom: 10,
            ),
            child: SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: SizedBox(
                width: max(
                  MediaQuery.of(context).size.width - 40,
                  validData.length * 50.0,
                ),
                child: LineChart(
                  LineChartData(
                    gridData: FlGridData(
                      show: true,
                      drawVerticalLine: true,
                      horizontalInterval: intervalY,
                      verticalInterval: 5,
                      getDrawingVerticalLine: (_) => const FlLine(
                        color: Color(0xff37434d),
                        strokeWidth: 0.2,
                      ),
                      getDrawingHorizontalLine: (_) => const FlLine(
                        color: Color(0xff37434d),
                        strokeWidth: 0.2,
                      ),
                    ),
                    titlesData: FlTitlesData(
                      show: true,
                      bottomTitles: AxisTitles(
                        sideTitles: SideTitles(
                          showTitles: true,
                          reservedSize: 30,
                          interval: 5,
                          getTitlesWidget: (value, meta) {
                            return SideTitleWidget(
                              axisSide: meta.axisSide,
                              space: 4,
                              child: Text(
                                '${(value - minX).toInt()}s',
                                style: const TextStyle(
                                  fontSize: 10,
                                  color: Colors.blueGrey,
                                ),
                              ),
                            );
                          },
                        ),
                      ),
                      leftTitles: AxisTitles(
                        sideTitles: SideTitles(
                          showTitles: true,
                          interval: intervalY,
                          reservedSize: 40,
                          getTitlesWidget: (value, meta) {
                            return SideTitleWidget(
                              axisSide: meta.axisSide,
                              space: 4,
                              child: Text(
                                _isTemperatureChart
                                    ? value.toStringAsFixed(1)
                                    : value.toInt().toString(),
                                style: const TextStyle(
                                  fontSize: 10,
                                  color: Colors.blueGrey,
                                ),
                              ),
                            );
                          },
                        ),
                      ),
                      topTitles: const AxisTitles(
                        sideTitles: SideTitles(showTitles: false),
                      ),
                      rightTitles: const AxisTitles(
                        sideTitles: SideTitles(showTitles: false),
                      ),
                    ),
                    extraLinesData: ExtraLinesData(
                      verticalLines: [
                        VerticalLine(
                          x: lastData.timestamp,
                          strokeWidth: 0,
                          label: VerticalLineLabel(
                            show: true,
                            alignment: const Alignment(-0.5, -0.5),
                            style: TextStyle(
                              color: _getColor(),
                              fontWeight: FontWeight.bold,
                              fontSize: 12,
                              backgroundColor: Colors.white70,
                            ),
                            labelResolver: (_) =>
                                '${lastValue.toStringAsFixed(_isTemperatureChart ? 2 : 1)}${_getUnit()}',
                          ),
                        ),
                      ],
                    ),
                    borderData: FlBorderData(
                      show: true,
                      border: Border.all(
                        color: const Color(0xff37434d),
                        width: 1,
                      ),
                    ),
                    minX: minX,
                    maxX: maxX,
                    minY: minY,
                    maxY: maxY,
                    lineBarsData: [
                      LineChartBarData(
                        spots: validData
                            .map((item) => FlSpot(item.timestamp, _getValue(item)))
                            .toList(),
                        isCurved: true,
                        color: _getColor(),
                        barWidth: 3,
                        isStrokeCapRound: true,
                        dotData: FlDotData(
                          show: true,
                          getDotPainter: (spot, percent, barData, index) {
                            return FlDotCirclePainter(
                              radius: 4,
                              color: _getColor(),
                              strokeWidth: 1,
                              strokeColor: Colors.white,
                            );
                          },
                        ),
                        belowBarData: BarAreaData(show: false),
                      ),
                    ],
                    lineTouchData: LineTouchData(enabled: false),
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class UserStatusScreen extends StatelessWidget {
  const UserStatusScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final isConnected = bleService.connectedDevice != null;

    return Container(
      color: Colors.blueGrey[50],
      padding: const EdgeInsets.only(top: 8),
      child: Column(
        children: [
          buildRoundedContainer(
            flex: 1,
            color: Colors.white,
            margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 9),
            child: isConnected
                ? SensorChart(
                    title: 'COMB_BPM 추이 (5초 간격)',
                    data: bleService.sensorHistory,
                    type: SensorType.combinedBpm,
                    intervalY: 10,
                  )
                : const Center(child: Text('BLE 연결 후 COMB_BPM 그래프가 표시됩니다.')),
          ),
          buildRoundedContainer(
            flex: 1,
            color: Colors.white,
            margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 9),
            child: isConnected
                ? SensorChart(
                    title: '체온 추이',
                    data: bleService.sensorHistory,
                    type: SensorType.temperature,
                    intervalY: 0.5,
                  )
                : const Center(child: Text('BLE 연결 후 체온 그래프가 표시됩니다.')),
          ),
        ],
      ),
    );
  }
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      requestBlePermissions(context);
    });
  }

  Widget _buildMeasurementStatus(BleService bleService) {
    if (bleService.baselineCombinedBpmMeanResult == null ||
        bleService.baselineBodyTemperatureResult == null) {
      return const SizedBox.shrink();
    }

    return Card(
      elevation: 4,
      color: Colors.green[50],
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'MEASUREMENT COMPLETED!',
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.bold,
                color: Colors.green,
              ),
            ),
            const SizedBox(height: 10),
            Text(
              'combBpm mean : ${bleService.baselineCombinedBpmMeanResult!.toStringAsFixed(1)} BPM',
              style: const TextStyle(fontSize: 18, color: Colors.black87),
            ),
            const SizedBox(height: 4),
            Text(
              '기초 체온 : ${bleService.baselineBodyTemperatureResult!.toStringAsFixed(2)} °C',
              style: const TextStyle(fontSize: 18, color: Colors.black87),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusFlags(BleService bleService) {
    if (!bleService.warningFlag && !bleService.activeFlag) {
      return const SizedBox.shrink();
    }

    final message = bleService.activeFlag ? 'ACTIVE!\n모터 구동됨' : 'WARNING!\n사용자 위험';
    final statusColor =
        bleService.activeFlag ? Colors.red.shade100 : Colors.yellow.shade100;
    final textColor =
        bleService.activeFlag ? Colors.red.shade800 : Colors.orange.shade800;

    return Card(
      elevation: 4,
      color: statusColor,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: Padding(
        padding: const EdgeInsets.all(20),
        child: Text(
          message,
          textAlign: TextAlign.center,
          style: TextStyle(
            fontSize: 26,
            fontWeight: FontWeight.w900,
            color: textColor,
            height: 1.4,
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final isConnected = bleService.connectedDevice != null;
    final connectionStatus = isConnected ? '✅ 데이터 수신 중' : '❌ BLE 연결 필요';
    final statusColor = isConnected ? Colors.blue[100]! : Colors.grey[300]!;

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Card(
            elevation: 4,
            color: statusColor,
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Column(
                children: [
                  const Text(
                    '장치 연결 상태',
                    style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.bold,
                      color: Colors.black87,
                    ),
                  ),
                  const SizedBox(height: 8),
                  Text(
                    connectionStatus,
                    style: TextStyle(
                      fontSize: 28,
                      fontWeight: FontWeight.w900,
                      color: isConnected ? Colors.blueAccent : Colors.grey,
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 20),
          _buildStatusFlags(bleService),
          const SizedBox(height: 20),
          _buildMeasurementStatus(bleService),
          const SizedBox(height: 20),
          ElevatedButton(
            style: ElevatedButton.styleFrom(
              backgroundColor: isConnected ? Colors.red : Colors.green,
              padding: const EdgeInsets.symmetric(vertical: 15),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(10),
              ),
            ),
            onPressed: () {
              if (isConnected) {
                bleService.disconnectDevice();
              } else {
                bleService.startScan();
              }
            },
            child: Text(
              isConnected ? '연결 해제' : 'BLE 장치 스캔 시작',
              style: const TextStyle(fontSize: 18, color: Colors.white),
            ),
          ),
          const SizedBox(height: 20),
          Text(
            '스캔된 장치 (${bleService.scanResults.length})',
            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
          ),
          ListView.builder(
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            itemCount: bleService.scanResults.length,
            itemBuilder: (context, index) {
              final result = bleService.scanResults[index];
              final deviceName = result.device.platformName.isNotEmpty
                  ? result.device.platformName
                  : '이름 없음';

              return Card(
                elevation: 1,
                margin: const EdgeInsets.symmetric(vertical: 8),
                child: ListTile(
                  title: Text(deviceName),
                  subtitle: Text(result.device.remoteId.toString()),
                  trailing: Text('RSSI: ${result.rssi}'),
                  onTap: () => bleService.connectToDevice(result.device),
                ),
              );
            },
          ),
        ],
      ),
    );
  }
}

class UserLocationScreen extends StatefulWidget {
  const UserLocationScreen({super.key});

  @override
  State<UserLocationScreen> createState() => _UserLocationScreenState();
}

class _UserLocationScreenState extends State<UserLocationScreen> {
  GoogleMapController? _mapController;
  LatLng? _lastCameraPosition;

  @override
  Widget build(BuildContext context) {
    final bleService = context.watch<BleService>();
    final latitude = bleService.latitude;
    final longitude = bleService.longitude;

    if (latitude == null || longitude == null) {
      return Container(
        color: Colors.blueGrey[50],
        child: const Center(
          child: Text('📡 BLE GPS 데이터를 기다리는 중...'),
        ),
      );
    }

    final currentPosition = LatLng(latitude, longitude);
    final madeAngle = bleService.madeAngle.trim().isNotEmpty
        ? bleService.madeAngle
        : '데이터 수신 대기 중';
    final predictionText = '사용자의 이동방향 예측 : $madeAngle';
    final markerRotation = bleService.predictedDirection ?? 0.0;

    if (_mapController != null && _lastCameraPosition != currentPosition) {
      _lastCameraPosition = currentPosition;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        _mapController?.animateCamera(
          CameraUpdate.newLatLngZoom(currentPosition, 15),
        );
      });
    }

    return Stack(
      children: [
        GoogleMap(
          onMapCreated: (controller) {
            _mapController = controller;
            _lastCameraPosition = currentPosition;
          },
          initialCameraPosition: CameraPosition(
            target: currentPosition,
            zoom: 15,
          ),
          myLocationEnabled: false,
          myLocationButtonEnabled: false,
          markers: {
            Marker(
              markerId: const MarkerId('lifevest_ble_location'),
              position: currentPosition,
              rotation: markerRotation,
              infoWindow: const InfoWindow(title: 'BLE 구명조끼 위치'),
              flat: true,
            ),
          },
        ),
        Positioned(
          top: 20,
          left: 20,
          right: 20,
          child: Container(
            padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 12),
            decoration: BoxDecoration(
              color: Colors.blueAccent.withOpacity(0.8),
              borderRadius: BorderRadius.circular(12),
              boxShadow: const [
                BoxShadow(
                  color: Colors.black26,
                  blurRadius: 4,
                  offset: Offset(0, 2),
                ),
              ],
            ),
            child: Text(
              predictionText,
              textAlign: TextAlign.center,
              style: const TextStyle(
                color: Colors.white,
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class HeartRateScreen extends StatelessWidget {
  const HeartRateScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final ppgBpm = context.watch<BleService>().ppgBpm;

    return Container(
      color: Colors.blueGrey[50],
      width: double.infinity,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Text(
            '실시간 심박수',
            style: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.bold,
              color: Colors.blueGrey,
            ),
          ),
          const SizedBox(height: 20),
          buildRoundedContainer(
            height: 200,
            color: Colors.white,
            child: Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.favorite, color: Colors.redAccent, size: 50),
                  const SizedBox(height: 10),
                  Text(
                    ppgBpm.toStringAsFixed(1),
                    style: const TextStyle(
                      fontSize: 60,
                      fontWeight: FontWeight.bold,
                      color: Colors.black87,
                    ),
                  ),
                  const Text(
                    'BPM',
                    style: TextStyle(fontSize: 18, color: Colors.blueGrey),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}
