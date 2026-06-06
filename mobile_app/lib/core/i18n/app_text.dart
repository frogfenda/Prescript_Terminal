import '../models/terminal_state.dart';

class AppText {
  const AppText(this.language);

  final String language;

  bool get zh => language.toUpperCase() != 'EN';

  String pick(String chinese, String english) => zh ? chinese : english;

  String get appTitle => pick('指令终端', 'Prescript Uplink');
  String get online => pick('在线', 'ONLINE');
  String get offline => pick('离线', 'OFFLINE');
  String get connecting => pick('连接中', 'CONNECTING');
  String get scanning => pick('扫描中', 'SCANNING');
  String get unknown => pick('未知', 'UNKNOWN');
  String get unboundTerminal => pick('未绑定终端', 'Unbound terminal');
  String get deviceIdPending => pick('等待设备编号', 'Device ID pending');
  String get userIdPending => pick('等待用户编号', 'User ID pending');
  String get currentDeviceOnly => pick('当前绑定设备', 'Current bound device');
  String get refresh => pick('刷新', 'Refresh');
  String get delete => pick('删除', 'Delete');
  String get inject => pick('注入', 'Inject');
  String get trigger => pick('触发', 'Trigger');
  String get read => pick('读取', 'Read');
  String get sync => pick('同步数据', 'Sync');
  String get connect => pick('连接终端', 'Connect');
  String get disconnect => pick('断开链路', 'Disconnect');
  String get addAndSync => pick('添加并同步', 'Add and sync');
  String get saveAndSync => pick('保存并同步', 'Save and sync');
  String get overwriteAndSync => pick('覆盖并同步', 'Overwrite and sync');

  String languageLabel(String value) {
    final normalized = value.toUpperCase();
    if (!zh) return normalized;
    return switch (normalized) {
      'ZH' => '中文',
      'EN' => '英文',
      _ => normalized,
    };
  }

  String statusLabel(TerminalConnectionStatus status) {
    return switch (status) {
      TerminalConnectionStatus.disconnected => offline,
      TerminalConnectionStatus.scanning => scanning,
      TerminalConnectionStatus.connecting => connecting,
      TerminalConnectionStatus.connected => online,
    };
  }

  String modeLabel(String mode) {
    final normalized = mode.toUpperCase();
    if (!zh) return normalized;
    return switch (normalized) {
      'UNKNOWN' => '未知模式',
      'RUNTIME' => '运行模式',
      'ZH' => '中文',
      'EN' => '英文',
      _ => normalized,
    };
  }

  String logLine(String line) {
    if (!zh) {
      return line;
    }
    if (line.startsWith('TX ERR ')) {
      return '发送失败';
    }
    if (line.startsWith('TX ')) {
      return _commandSummary(line.substring(3), sending: true);
    }
    if (line.startsWith('RX ')) {
      return _commandSummary(line.substring(3), sending: false);
    }
    return line;
  }

  String _commandSummary(String payload, {required bool sending}) {
    final prefix = sending ? '发送' : '接收';
    if (payload.startsWith('GET:SYNC')) {
      return '$prefix 同步请求';
    }
    if (payload.startsWith('TXT:')) {
      return '$prefix 即时指令';
    }
    if (payload.startsWith('PRE:') || payload.startsWith('PRE_DEL:')) {
      return '$prefix 指令池变更';
    }
    if (payload.startsWith('SPC') || payload.startsWith('GET:SPC')) {
      return '$prefix 特异点数据';
    }
    if (payload.startsWith('WIFI:')) {
      return '$prefix 无线网络配置';
    }
    if (payload.startsWith('ALM') ||
        payload.startsWith('SCH') ||
        payload.startsWith('POM') ||
        payload.startsWith('COIN')) {
      return '$prefix 调度变更';
    }
    if (payload.startsWith('LANG:')) {
      return '$prefix 语言状态';
    }
    if (payload.startsWith('ACK:')) {
      return '$prefix 设备确认';
    }
    if (payload.startsWith('SYNC:')) {
      return '$prefix 同步数据';
    }
    return '$prefix 链路数据';
  }
}
