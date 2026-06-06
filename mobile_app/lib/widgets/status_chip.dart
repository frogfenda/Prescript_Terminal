import 'package:flutter/material.dart';

import '../core/theme/terminal_theme.dart';

class StatusChip extends StatelessWidget {
  const StatusChip({
    required this.label,
    this.color = TerminalColors.cyan,
    super.key,
  });

  final String label;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        border: Border.all(color: color),
      ),
      child: Text(
        label,
        style:
            TextStyle(color: color, fontSize: 12, fontWeight: FontWeight.w700),
      ),
    );
  }
}
