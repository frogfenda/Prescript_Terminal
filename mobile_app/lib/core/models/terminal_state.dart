import 'bound_device.dart';
import 'terminal_records.dart';

enum TerminalConnectionStatus {
  disconnected,
  scanning,
  connecting,
  connected,
}

class TerminalState {
  const TerminalState({
    this.connectionStatus = TerminalConnectionStatus.disconnected,
    this.device,
    this.language = 'ZH',
    this.languageMode = 'UNKNOWN',
    this.buildCode = '',
    this.lastStatus = 'SYS_STATUS: OFFLINE',
    this.lastAck,
    this.logs = const [],
    this.alarms = const [],
    this.schedules = const [],
    this.prescripts = const [],
    this.coins = const [],
    this.prescriptTargets = const [],
    this.currentPrescriptTarget = '',
    this.specials = const {},
  });

  final TerminalConnectionStatus connectionStatus;
  final BoundDevice? device;
  final String language;
  final String languageMode;
  final String buildCode;
  final String lastStatus;
  final String? lastAck;
  final List<String> logs;
  final List<AlarmRecord> alarms;
  final List<ScheduleRecord> schedules;
  final List<PrescriptRecord> prescripts;
  final List<CoinRecord> coins;
  final List<String> prescriptTargets;
  final String currentPrescriptTarget;
  final Map<String, SpecialRecord> specials;

  bool get isConnected =>
      connectionStatus == TerminalConnectionStatus.connected;

  TerminalState copyWith({
    TerminalConnectionStatus? connectionStatus,
    BoundDevice? device,
    String? language,
    String? languageMode,
    String? buildCode,
    String? lastStatus,
    String? lastAck,
    List<String>? logs,
    List<AlarmRecord>? alarms,
    List<ScheduleRecord>? schedules,
    List<PrescriptRecord>? prescripts,
    List<CoinRecord>? coins,
    List<String>? prescriptTargets,
    String? currentPrescriptTarget,
    Map<String, SpecialRecord>? specials,
  }) {
    return TerminalState(
      connectionStatus: connectionStatus ?? this.connectionStatus,
      device: device ?? this.device,
      language: language ?? this.language,
      languageMode: languageMode ?? this.languageMode,
      buildCode: buildCode ?? this.buildCode,
      lastStatus: lastStatus ?? this.lastStatus,
      lastAck: lastAck ?? this.lastAck,
      logs: logs ?? this.logs,
      alarms: alarms ?? this.alarms,
      schedules: schedules ?? this.schedules,
      prescripts: prescripts ?? this.prescripts,
      coins: coins ?? this.coins,
      prescriptTargets: prescriptTargets ?? this.prescriptTargets,
      currentPrescriptTarget:
          currentPrescriptTarget ?? this.currentPrescriptTarget,
      specials: specials ?? this.specials,
    );
  }
}
