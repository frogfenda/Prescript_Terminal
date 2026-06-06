class BoundDevice {
  const BoundDevice({
    this.deviceId,
    required this.bleName,
    required this.displayName,
    this.language = 'ZH',
    this.firmwareVersion,
    this.capabilities = const [],
    this.lastSeenAt,
  });

  final String? deviceId;
  final String bleName;
  final String displayName;
  final String language;
  final String? firmwareVersion;
  final List<String> capabilities;
  final DateTime? lastSeenAt;

  BoundDevice copyWith({
    String? deviceId,
    String? bleName,
    String? displayName,
    String? language,
    String? firmwareVersion,
    List<String>? capabilities,
    DateTime? lastSeenAt,
  }) {
    return BoundDevice(
      deviceId: deviceId ?? this.deviceId,
      bleName: bleName ?? this.bleName,
      displayName: displayName ?? this.displayName,
      language: language ?? this.language,
      firmwareVersion: firmwareVersion ?? this.firmwareVersion,
      capabilities: capabilities ?? this.capabilities,
      lastSeenAt: lastSeenAt ?? this.lastSeenAt,
    );
  }
}
