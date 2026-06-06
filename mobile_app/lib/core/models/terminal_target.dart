enum TerminalTargetType {
  localDevice,
  deviceId,
  userId,
}

class TerminalTarget {
  const TerminalTarget({
    required this.type,
    required this.id,
    required this.label,
  });

  final TerminalTargetType type;
  final String id;
  final String label;

  static const currentDevice = TerminalTarget(
    type: TerminalTargetType.localDevice,
    id: 'current',
    label: 'Current bound device',
  );
}
