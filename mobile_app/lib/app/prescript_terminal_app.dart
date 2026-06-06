import 'package:flutter/material.dart';

import '../core/theme/terminal_theme.dart';
import 'terminal_shell.dart';

class PrescriptTerminalApp extends StatelessWidget {
  const PrescriptTerminalApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Prescript Terminal',
      debugShowCheckedModeBanner: false,
      theme: TerminalTheme.dark(),
      home: const TerminalShell(),
    );
  }
}
