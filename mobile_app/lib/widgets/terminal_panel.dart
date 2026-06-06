import 'package:flutter/material.dart';

import '../core/theme/terminal_theme.dart';

class TerminalPanel extends StatelessWidget {
  const TerminalPanel({
    required this.title,
    required this.child,
    this.action,
    super.key,
  });

  final String title;
  final Widget child;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    return Container(
      margin: const EdgeInsets.fromLTRB(12, 8, 12, 8),
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: TerminalColors.panel,
        border: Border.all(color: TerminalColors.border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  title,
                  style: const TextStyle(
                    color: TerminalColors.muted,
                    fontWeight: FontWeight.w700,
                    fontSize: 13,
                  ),
                ),
              ),
              if (action != null) action!,
            ],
          ),
          const Divider(color: Color(0xFF123A3A)),
          child,
        ],
      ),
    );
  }
}
