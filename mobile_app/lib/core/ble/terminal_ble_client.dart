import 'dart:async';
import 'dart:convert';

import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../protocol/terminal_protocol.dart';

class TerminalBleClient {
  TerminalBleClient();

  final StreamController<String> _messages =
      StreamController<String>.broadcast();
  final StreamController<bool> _connection = StreamController<bool>.broadcast();

  BluetoothDevice? _device;
  BluetoothCharacteristic? _characteristic;
  StreamSubscription<List<int>>? _notifySub;
  StreamSubscription<BluetoothConnectionState>? _connectionSub;

  Stream<String> get messages => _messages.stream;
  Stream<bool> get connectionChanges => _connection.stream;
  bool get isConnected => _characteristic != null;

  Future<void> connect(
      {Duration scanTimeout = const Duration(seconds: 8)}) async {
    await disconnect();

    final completer = Completer<BluetoothDevice>();
    late final StreamSubscription<List<ScanResult>> scanSub;

    scanSub = FlutterBluePlus.scanResults.listen((results) {
      for (final result in results) {
        final name = result.device.platformName;
        if (name == terminalBleName && !completer.isCompleted) {
          completer.complete(result.device);
          break;
        }
      }
    });

    BluetoothDevice device;
    try {
      await FlutterBluePlus.startScan(
        withNames: [terminalBleName],
        timeout: scanTimeout,
      );
      device = await completer.future.timeout(scanTimeout);
    } finally {
      await FlutterBluePlus.stopScan();
      await scanSub.cancel();
    }

    _device = device;
    _connectionSub = device.connectionState.listen((state) {
      final connected = state == BluetoothConnectionState.connected;
      _connection.add(connected);
      if (!connected) _characteristic = null;
    });

    await device.connect(
      license: License.nonprofit,
      timeout: const Duration(seconds: 12),
    );
    final services = await device.discoverServices();
    final targetServiceUuid = Guid(terminalServiceUuid);
    final targetCharacteristicUuid = Guid(terminalCharacteristicUuid);
    final service = services.firstWhere(
      (s) => s.uuid == targetServiceUuid,
    );
    final characteristic = service.characteristics.firstWhere(
      (c) => c.uuid == targetCharacteristicUuid,
    );

    _characteristic = characteristic;
    await characteristic.setNotifyValue(true);
    _notifySub = characteristic.onValueReceived.listen((value) {
      _messages.add(utf8.decode(value));
    });
    _connection.add(true);
  }

  Future<void> write(String command) async {
    final characteristic = _characteristic;
    if (characteristic == null) {
      throw StateError('Terminal is not connected');
    }
    await characteristic.write(utf8.encode(command), withoutResponse: false);
  }

  Future<void> disconnect() async {
    await _notifySub?.cancel();
    await _connectionSub?.cancel();
    _notifySub = null;
    _connectionSub = null;
    _characteristic = null;
    final device = _device;
    _device = null;
    if (device != null) {
      try {
        await device.disconnect();
      } catch (_) {
        // Device may already be disconnected.
      }
    }
    _connection.add(false);
  }

  Future<void> dispose() async {
    await disconnect();
    await _messages.close();
    await _connection.close();
  }
}
