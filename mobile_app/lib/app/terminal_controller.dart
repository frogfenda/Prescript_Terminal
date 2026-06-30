import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;

import '../core/ble/terminal_ble_client.dart';
import '../core/models/bound_device.dart';
import '../core/models/terminal_records.dart';
import '../core/models/terminal_state.dart';
import '../core/protocol/terminal_protocol.dart';
import '../core/storage/upload_state_store.dart';

class TerminalController extends ChangeNotifier {
  TerminalController({
    TerminalBleClient? bleClient,
    UploadStateStore? uploadStateStore,
  })  : _ble = bleClient ?? TerminalBleClient(),
        _uploadStore = uploadStateStore ?? UploadStateStore() {
    _messageSub = _ble.messages.listen(_handleRawMessage);
    _connectionSub = _ble.connectionChanges.listen((connected) {
      state = state.copyWith(
        connectionStatus: connected
            ? TerminalConnectionStatus.connected
            : TerminalConnectionStatus.disconnected,
        lastStatus: connected ? 'SYS_STATUS: ONLINE' : 'SYS_STATUS: OFFLINE',
      );
      notifyListeners();
    });
    loadUploadedItems();
  }

  final TerminalBleClient _ble;
  final UploadStateStore _uploadStore;
  final TerminalProtocolParser _parser = TerminalProtocolParser();
  StreamSubscription<String>? _messageSub;
  StreamSubscription<bool>? _connectionSub;
  final List<String> _pendingAckSyncScopes = [];
  Timer? _syncTimer;
  final Map<String, Timer> _scopeSyncTimers = {};

  TerminalState state = const TerminalState();
  List<CloudDirective> cloudDirectives = const [];
  Set<String> uploadedItems = {};

  Future<void> connect() async {
    state = state.copyWith(
      connectionStatus: TerminalConnectionStatus.connecting,
      lastStatus: 'SYS_STATUS: CONNECTING',
    );
    notifyListeners();
    await _ble.connect();
    state = state.copyWith(
      device: const BoundDevice(
        bleName: terminalBleName,
        displayName: terminalBleName,
      ),
      connectionStatus: TerminalConnectionStatus.connected,
      lastStatus: 'SYS_STATUS: ONLINE',
    );
    notifyListeners();
    await sendRaw(TerminalCommands.getLanguage(), syncAfter: false);
    Future<void>.delayed(const Duration(milliseconds: 650), () {
      if (state.languageMode == 'UNKNOWN') requestSync();
    });
  }

  Future<void> disconnect() => _ble.disconnect();

  Future<void> sendRaw(String command,
      {bool? syncAfter, String? syncScope}) async {
    final shouldSync = syncAfter ?? TerminalCommands.commandNeedsSync(command);
    if (shouldSync) {
      _pendingAckSyncScopes.add(
          (syncScope ?? TerminalCommands.syncScopeForCommand(command))
              .toUpperCase());
    }
    try {
      await _ble.write(command);
      _appendLog('TX $command');
    } catch (error) {
      if (shouldSync && _pendingAckSyncScopes.isNotEmpty) {
        _pendingAckSyncScopes.removeAt(0);
      }
      _appendLog('TX ERR $error');
      rethrow;
    }
  }

  void requestSync({Duration delay = const Duration(milliseconds: 180)}) {
    _syncTimer?.cancel();
    for (final timer in _scopeSyncTimers.values) {
      timer.cancel();
    }
    _scopeSyncTimers.clear();
    _syncTimer = Timer(delay, () {
      sendRaw(TerminalCommands.sync(state.language), syncAfter: false);
    });
  }

  void requestSyncScope(String scope,
      {Duration delay = const Duration(milliseconds: 180)}) {
    final normalized = scope.toUpperCase();
    if (normalized == 'ALL') {
      requestSync(delay: delay);
      return;
    }
    _scopeSyncTimers[normalized]?.cancel();
    _scopeSyncTimers[normalized] = Timer(delay, () {
      _scopeSyncTimers.remove(normalized);
      sendRaw(TerminalCommands.syncScope(normalized), syncAfter: false);
    });
  }

  Future<void> loadCloudArchive() async {
    try {
      final response = await http
          .get(Uri.parse('https://index.dimension-404.cloud/api/all'));
      final decoded = jsonDecode(response.body);
      if (decoded is List) {
        cloudDirectives = decoded
            .whereType<Map<String, dynamic>>()
            .map(CloudDirective.fromJson)
            .where((item) => item.content.isNotEmpty)
            .toList();
      }
    } catch (_) {
      cloudDirectives = const [
        CloudDirective(
            content: '致...在https://index.dimension-404.cloud/submit发布指令',
            author: 'Anonymous'),
        CloudDirective(content: '致…前往L公司，寻找金枝...', author: 'Dante'),
        CloudDirective(content: '在夜晚降临前，清理掉街道上所有的监控探头。', author: 'System'),
      ];
    }
    notifyListeners();
  }

  Future<void> loadUploadedItems() async {
    uploadedItems = await _uploadStore.load(state.language);
    notifyListeners();
  }

  Future<void> _saveUploadedItems() async {
    await _uploadStore.save(state.language, uploadedItems);
  }

  Future<void> injectCloudDirective(CloudDirective item) async {
    await sendRaw(TerminalCommands.addPrescript(state.language, item.content),
        syncAfter: true);
    uploadedItems = {...uploadedItems, item.content};
    await _saveUploadedItems();
    notifyListeners();
  }

  Future<void> removeCloudDirective(CloudDirective item) async {
    await sendRaw(
        TerminalCommands.deletePrescript(state.language, item.content),
        syncAfter: true);
    uploadedItems = {...uploadedItems}..remove(item.content);
    await _saveUploadedItems();
    notifyListeners();
  }

  Future<void> injectRandomCloudDirectives({int count = 10}) async {
    final available = cloudDirectives
        .where((item) => !uploadedItems.contains(item.content))
        .toList()
      ..shuffle();
    final picked = available.take(count).toList();
    for (final item in picked) {
      await sendRaw(TerminalCommands.addPrescript(state.language, item.content),
          syncAfter: false);
      uploadedItems = {...uploadedItems, item.content};
      await Future<void>.delayed(const Duration(milliseconds: 100));
    }
    await _saveUploadedItems();
    requestSync(delay: const Duration(milliseconds: 250));
    notifyListeners();
  }

  Future<void> addPrescriptTarget(String id) async {
    await sendRaw(TerminalCommands.addTarget(id), syncAfter: true);
  }

  Future<void> deletePrescriptTarget(String id) async {
    await sendRaw(TerminalCommands.deleteTarget(id), syncAfter: true);
  }

  Future<void> setPrescriptTarget(String id) async {
    await sendRaw(TerminalCommands.setTarget(id), syncAfter: true);
  }

  void _handleRawMessage(String raw) {
    _appendLog('RX $raw', notify: false);
    final message = _parser.parse(raw);
    switch (message) {
      case LangMessage():
        final lang = message.language.toUpperCase();
        state = state.copyWith(
          language: lang,
          languageMode: message.mode,
          buildCode: message.buildCode,
          lastStatus: 'SYS_STATUS: ONLINE [$lang / ${message.mode}]',
          device: state.device?.copyWith(
            language: lang,
            firmwareVersion:
                message.buildCode.isEmpty ? null : message.buildCode,
            lastSeenAt: DateTime.now(),
          ),
        );
        loadUploadedItems();
        requestSync(delay: const Duration(milliseconds: 120));
      case AckMessage():
        state = state.copyWith(
          lastAck: message.raw,
          lastStatus: 'ACK ${message.level}: ${message.label}',
        );
        if (_pendingAckSyncScopes.isNotEmpty) {
          final scope = _pendingAckSyncScopes.removeAt(0);
          if (message.level != 'ERR') {
            requestSyncScope(scope, delay: const Duration(milliseconds: 160));
          }
        }
      case SyncClearMessage():
        _clearSyncScope(message.scope);
      case AlarmSyncMessage():
        state = state.copyWith(alarms: [...state.alarms, message.record]);
      case ScheduleSyncMessage():
        state = state.copyWith(schedules: [...state.schedules, message.record]);
      case PrescriptSyncMessage():
        state =
            state.copyWith(prescripts: [...state.prescripts, message.record]);
      case CoinSyncMessage():
        state = state.copyWith(coins: [...state.coins, message.record]);
      case PomodoroSyncMessage():
        state = state.copyWith(pomodoros: [...state.pomodoros, message.record]);
      case TargetSyncMessage():
        state = state.copyWith(
          prescriptTargets: message.items,
          currentPrescriptTarget: message.current,
        );
      case SpecialMetaMessage():
        state = state.copyWith(
          specials: {...state.specials, message.record.id: message.record},
        );
      case SpecialTextMessage():
        final existing = state.specials[message.id];
        if (existing != null) {
          state = state.copyWith(
            specials: {
              ...state.specials,
              message.id: existing.copyWith(fetchedText: message.text)
            },
          );
        }
      case RawTerminalMessage():
        state = state.copyWith(lastStatus: message.raw);
    }
    notifyListeners();
  }

  void _clearSyncScope(String scope) {
    switch (scope.toUpperCase()) {
      case 'ALM':
        state = state.copyWith(alarms: const []);
      case 'SCH':
        state = state.copyWith(schedules: const []);
      case 'COIN':
        state = state.copyWith(coins: const []);
      case 'POM':
        state = state.copyWith(pomodoros: const []);
      case 'SPC':
        state = state.copyWith(specials: const {});
      case 'TGT':
        state = state.copyWith(
          prescriptTargets: const [],
          currentPrescriptTarget: '',
        );
      default:
        state = state.copyWith(
          alarms: const [],
          schedules: const [],
          prescripts: const [],
          coins: const [],
          pomodoros: const [],
          prescriptTargets: const [],
          currentPrescriptTarget: '',
          specials: const {},
        );
    }
  }

  void _appendLog(String line, {bool notify = true}) {
    final next = [line, ...state.logs].take(80).toList();
    state = state.copyWith(logs: next);
    if (notify) notifyListeners();
  }

  @override
  void dispose() {
    _syncTimer?.cancel();
    for (final timer in _scopeSyncTimers.values) {
      timer.cancel();
    }
    _messageSub?.cancel();
    _connectionSub?.cancel();
    _ble.dispose();
    super.dispose();
  }
}
