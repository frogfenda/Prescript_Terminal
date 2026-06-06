import 'package:flutter/material.dart';

import '../core/theme/terminal_theme.dart';

class TerminalButton extends StatelessWidget {
  const TerminalButton({
    required this.label,
    required this.onPressed,
    this.destructive = false,
    this.compact = false,
    super.key,
  });

  final String label;
  final VoidCallback? onPressed;
  final bool destructive;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final color = destructive ? TerminalColors.red : TerminalColors.cyan;
    return OutlinedButton(
      onPressed: onPressed,
      style: OutlinedButton.styleFrom(
        foregroundColor: color,
        side: BorderSide(color: color),
        padding: EdgeInsets.symmetric(
          horizontal: compact ? 10 : 14,
          vertical: compact ? 8 : 12,
        ),
        shape: const RoundedRectangleBorder(),
      ),
      child: Text(
        label,
        style: TextStyle(fontSize: compact ? 12 : 14),
      ),
    );
  }
}
