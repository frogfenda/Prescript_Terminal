import 'package:flutter/material.dart';

import 'terminal_controller.dart';

class TerminalScope extends InheritedNotifier<TerminalController> {
  const TerminalScope({
    required TerminalController controller,
    required super.child,
    super.key,
  }) : super(notifier: controller);

  static TerminalController of(BuildContext context) {
    final scope = context.dependOnInheritedWidgetOfExactType<TerminalScope>();
    assert(scope != null, 'TerminalScope not found');
    return scope!.notifier!;
  }
}
