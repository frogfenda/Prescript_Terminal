class AlarmRecord {
  const AlarmRecord({
    required this.hour,
    required this.minute,
    required this.name,
    required this.text,
  });

  final int hour;
  final int minute;
  final String name;
  final String text;

  factory AlarmRecord.fromJson(Map<String, dynamic> json) {
    return AlarmRecord(
      hour: (json['h'] as num?)?.toInt() ?? 0,
      minute: (json['m'] as num?)?.toInt() ?? 0,
      name: json['n']?.toString() ?? '',
      text: json['t']?.toString() ?? '',
    );
  }
}

class ScheduleRecord {
  const ScheduleRecord({
    required this.dateTime,
    required this.name,
    required this.text,
  });

  final String dateTime;
  final String name;
  final String text;

  factory ScheduleRecord.fromJson(Map<String, dynamic> json) {
    return ScheduleRecord(
      dateTime: json['dt']?.toString() ?? '',
      name: json['n']?.toString() ?? '',
      text: json['t']?.toString() ?? '',
    );
  }
}

class CoinRecord {
  const CoinRecord({
    required this.basePower,
    required this.coinPower,
    required this.count,
    required this.colors,
    required this.name,
  });

  final int basePower;
  final int coinPower;
  final int count;
  final String colors;
  final String name;

  factory CoinRecord.fromJson(Map<String, dynamic> json) {
    return CoinRecord(
      basePower: (json['bp'] as num?)?.toInt() ?? 0,
      coinPower: (json['cp'] as num?)?.toInt() ?? 0,
      count: (json['cc'] as num?)?.toInt() ?? 0,
      colors: json['cl']?.toString() ?? '',
      name: json['n']?.toString() ?? '',
    );
  }
}

class PrescriptRecord {
  const PrescriptRecord({
    required this.text,
  });

  final String text;

  factory PrescriptRecord.fromJson(Map<String, dynamic> json) {
    return PrescriptRecord(
        text: json['t']?.toString() ?? json['text']?.toString() ?? '');
  }
}

class SpecialRecord {
  const SpecialRecord({
    required this.kind,
    required this.id,
    required this.name,
    required this.probability,
    required this.progress,
    required this.colorHex,
    required this.popupTitle,
    required this.enabled,
    this.fetchedText,
  });

  final String kind;
  final String id;
  final String name;
  final String probability;
  final String progress;
  final String colorHex;
  final String popupTitle;
  final bool enabled;
  final String? fetchedText;

  SpecialRecord copyWith({String? fetchedText}) {
    return SpecialRecord(
      kind: kind,
      id: id,
      name: name,
      probability: probability,
      progress: progress,
      colorHex: colorHex,
      popupTitle: popupTitle,
      enabled: enabled,
      fetchedText: fetchedText ?? this.fetchedText,
    );
  }

  factory SpecialRecord.fromMetaParts(List<String> parts) {
    return SpecialRecord(
      kind: parts.isNotEmpty ? parts[0] : '',
      id: parts.length > 1 ? parts[1] : '',
      name: parts.length > 2 ? parts[2] : '',
      probability: parts.length > 3 ? parts[3] : '',
      progress: parts.length > 4 ? parts[4] : '',
      colorHex: parts.length > 5 ? parts[5] : '0x07FF',
      popupTitle: parts.length > 6 ? parts[6] : '',
      enabled: parts.length > 7 ? parts[7] == '1' : true,
    );
  }
}

class CloudDirective {
  const CloudDirective({
    required this.content,
    required this.author,
  });

  final String content;
  final String author;

  factory CloudDirective.fromJson(Map<String, dynamic> json) {
    return CloudDirective(
      content: json['content']?.toString() ?? '',
      author: json['author']?.toString() ?? 'UNKNOWN',
    );
  }
}
