import 'dart:convert';

import '../models/terminal_records.dart';

const terminalBleName = 'Terminal_01';
const terminalServiceUuid = '0000dead-0000-1000-8000-00805f9b34fb';
const terminalCharacteristicUuid = '0000beef-0000-1000-8000-00805f9b34fb';

String sanitizeCommandField(String value, {bool keepColon = false}) {
  var out = value.replaceAll(RegExp(r'\r?\n'), ' ');
  out = out.replaceAll('|', '｜');
  if (!keepColon) out = out.replaceAll(':', '：');
  return out.trim();
}

class TerminalCommands {
  static String getLanguage() => 'GET:LANG';
  static String sync(String lang) => 'GET:SYNC:${lang.toUpperCase()}';
  static String getSpecialText(String id) =>
      'GET:SPC_TXT:${sanitizeCommandField(id, keepColon: true)}';
  static String textNotify(String text) => 'TXT:${sanitizeCommandField(text)}';
  static String forceSpecial(String id) =>
      'SPC:${sanitizeCommandField(id, keepColon: true)}';
  static String addPrescript(String lang, String text) =>
      'PRE:${lang.toUpperCase()}:${sanitizeCommandField(text)}';
  static String deletePrescript(String lang, String text) =>
      'PRE_DEL:${lang.toUpperCase()}:${sanitizeCommandField(text)}';
  static String setWifi(String ssid, String password) {
    return 'WIFI:${sanitizeCommandField(ssid, keepColon: true)}:${sanitizeCommandField(password, keepColon: true)}';
  }

  static String addAlarm({
    required int hour,
    required int minute,
    required String name,
    required String text,
  }) {
    return 'ALM:${hour.toString().padLeft(2, '0')}:${minute.toString().padLeft(2, '0')}:'
        '${sanitizeCommandField(name)}:${sanitizeCommandField(text)}';
  }

  static String deleteAlarm(String name) =>
      'ALM_DEL:${sanitizeCommandField(name)}';

  static String addSchedule({
    required DateTime dateTime,
    required String title,
    required String text,
  }) {
    return 'SCH:${dateTime.year}:${dateTime.month}:${dateTime.day}:${dateTime.hour}:${dateTime.minute}:'
        '${sanitizeCommandField(title)}:${sanitizeCommandField(text)}';
  }

  static String deleteSchedule(String title) =>
      'SCH_DEL:${sanitizeCommandField(title)}';

  static String updatePomodoro({
    required int slot,
    required String name,
    required int workMinutes,
    required int restMinutes,
  }) {
    return 'POM:$slot:${sanitizeCommandField(name)}:$workMinutes:$restMinutes';
  }

  static String addCoin({
    required int basePower,
    required int coinPower,
    required int count,
    required String colors,
    required String name,
  }) {
    return 'COIN:$basePower:$coinPower:$count:$colors:${sanitizeCommandField(name)}';
  }

  static String deleteCoin(String name) =>
      'COIN_DEL:${sanitizeCommandField(name)}';
  static String addTarget(String id) =>
      'TGT_ADD:${sanitizeCommandField(id, keepColon: true)}';
  static String deleteTarget(String id) =>
      'TGT_DEL:${sanitizeCommandField(id, keepColon: true)}';
  static String setTarget(String id) =>
      'TGT_SET:${sanitizeCommandField(id, keepColon: true)}';

  static bool commandNeedsSync(String command) {
    return RegExp(
                r'^(ALM:|ALM_DEL:|SCH:|SCH_DEL:|POM:|PRE:|PRE_DEL:|COIN:|COIN_DEL:)')
            .hasMatch(command) ||
        command.startsWith('TGT_');
  }
}

sealed class TerminalMessage {
  const TerminalMessage();
}

class LangMessage extends TerminalMessage {
  const LangMessage({
    required this.language,
    required this.mode,
    required this.buildCode,
  });

  final String language;
  final String mode;
  final String buildCode;
}

class AckMessage extends TerminalMessage {
  const AckMessage({
    required this.level,
    required this.label,
    required this.raw,
  });

  final String level;
  final String label;
  final String raw;
}

class SyncClearMessage extends TerminalMessage {
  const SyncClearMessage();
}

class AlarmSyncMessage extends TerminalMessage {
  const AlarmSyncMessage(this.record);
  final AlarmRecord record;
}

class ScheduleSyncMessage extends TerminalMessage {
  const ScheduleSyncMessage(this.record);
  final ScheduleRecord record;
}

class PrescriptSyncMessage extends TerminalMessage {
  const PrescriptSyncMessage(this.record);
  final PrescriptRecord record;
}

class CoinSyncMessage extends TerminalMessage {
  const CoinSyncMessage(this.record);
  final CoinRecord record;
}

class TargetSyncMessage extends TerminalMessage {
  const TargetSyncMessage({
    required this.current,
    required this.items,
  });

  final String current;
  final List<String> items;
}

class SpecialMetaMessage extends TerminalMessage {
  const SpecialMetaMessage(this.record);
  final SpecialRecord record;
}

class SpecialTextMessage extends TerminalMessage {
  const SpecialTextMessage({
    required this.id,
    required this.text,
  });

  final String id;
  final String text;
}

class RawTerminalMessage extends TerminalMessage {
  const RawTerminalMessage(this.raw);
  final String raw;
}

class TerminalProtocolParser {
  TerminalMessage parse(String raw) {
    if (raw.startsWith('LANG:')) {
      final parts = raw.split(':');
      return LangMessage(
        language: parts.length > 1 ? parts[1].toUpperCase() : 'ZH',
        mode: parts.length > 2 ? parts[2].toUpperCase() : 'RUNTIME',
        buildCode: parts.length > 3 ? parts[3] : '',
      );
    }
    if (raw.startsWith('ACK:OK:') ||
        raw.startsWith('ACK:WARN:') ||
        raw.startsWith('ACK:ERR:')) {
      final parts = raw.split(':');
      final level = parts.length > 1 ? parts[1] : 'OK';
      final label =
          parts.length > 2 ? raw.substring('ACK:$level:'.length) : raw;
      return AckMessage(level: level, label: label, raw: raw);
    }
    if (raw == 'SYNC:CLEAR') return const SyncClearMessage();
    if (raw.startsWith('SYNC:ALM:')) {
      return _safeJson(raw, 'SYNC:ALM:',
          (json) => AlarmSyncMessage(AlarmRecord.fromJson(json)));
    }
    if (raw.startsWith('SYNC:SCH:')) {
      return _safeJson(raw, 'SYNC:SCH:',
          (json) => ScheduleSyncMessage(ScheduleRecord.fromJson(json)));
    }
    if (raw.startsWith('SYNC:PRE:')) {
      return _safeJson(raw, 'SYNC:PRE:',
          (json) => PrescriptSyncMessage(PrescriptRecord.fromJson(json)));
    }
    if (raw.startsWith('SYNC:COIN:')) {
      return _safeJson(raw, 'SYNC:COIN:',
          (json) => CoinSyncMessage(CoinRecord.fromJson(json)));
    }
    if (raw.startsWith('SYNC:TGT:')) {
      return _safeJson(raw, 'SYNC:TGT:', (json) {
        final items = json['items'];
        return TargetSyncMessage(
          current: json['current']?.toString() ?? '',
          items: items is List
              ? items.map((item) => item.toString()).toList()
              : const [],
        );
      });
    }
    if (raw.startsWith('SPC_META:')) {
      return SpecialMetaMessage(
          SpecialRecord.fromMetaParts(raw.substring(9).split('|')));
    }
    if (raw.startsWith('SPC_TXT:')) {
      final idx = raw.indexOf('|');
      if (idx > 8) {
        return SpecialTextMessage(
            id: raw.substring(8, idx), text: raw.substring(idx + 1));
      }
    }
    return RawTerminalMessage(raw);
  }

  TerminalMessage _safeJson(
    String raw,
    String prefix,
    TerminalMessage Function(Map<String, dynamic> json) build,
  ) {
    try {
      final payload = raw.substring(prefix.length);
      final decoded = jsonDecode(payload);
      if (decoded is Map<String, dynamic>) return build(decoded);
    } catch (_) {
      // Keep the notify stream alive even if a firmware-side JSON string is malformed.
    }
    return RawTerminalMessage(raw);
  }
}
