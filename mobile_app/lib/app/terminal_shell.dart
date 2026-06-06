import 'package:flutter/material.dart';

import '../core/i18n/app_text.dart';
import '../core/theme/terminal_theme.dart';
import '../features/device/device_screen.dart';
import '../features/directives/directives_screen.dart';
import '../features/network/network_screen.dart';
import '../features/schedule/schedule_screen.dart';
import '../features/terminal/terminal_screen.dart';
import 'terminal_controller.dart';
import 'terminal_scope.dart';

class TerminalShell extends StatefulWidget {
  const TerminalShell({super.key});

  @override
  State<TerminalShell> createState() => _TerminalShellState();
}

class _TerminalShellState extends State<TerminalShell> {
  late final TerminalController controller;
  int index = 0;

  final screens = const [
    TerminalScreen(),
    DirectivesScreen(),
    ScheduleScreen(),
    NetworkScreen(),
    DeviceScreen(),
  ];

  @override
  void initState() {
    super.initState();
    controller = TerminalController();
    controller.loadCloudArchive();
  }

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return TerminalScope(
      controller: controller,
      child: AnimatedBuilder(
        animation: controller,
        builder: (context, _) {
          final state = controller.state;
          final text = AppText(state.language);
          final color =
              state.isConnected ? TerminalColors.green : TerminalColors.red;
          return Scaffold(
            appBar: AppBar(
              title: Text(text.appTitle),
              actions: [
                Padding(
                  padding: const EdgeInsets.only(right: 12),
                  child: Center(
                    child: Text(
                      state.isConnected ? text.online : text.offline,
                      style:
                          TextStyle(color: color, fontWeight: FontWeight.w700),
                    ),
                  ),
                ),
              ],
            ),
            body: screens[index],
            bottomNavigationBar: BottomNavigationBar(
              currentIndex: index,
              onTap: (value) => setState(() => index = value),
              items: [
                BottomNavigationBarItem(
                    icon: const Icon(Icons.memory),
                    label: text.pick('终端', 'Terminal')),
                BottomNavigationBarItem(
                    icon: const Icon(Icons.description),
                    label: text.pick('指令', 'Directives')),
                BottomNavigationBarItem(
                    icon: const Icon(Icons.schedule),
                    label: text.pick('调度', 'Schedule')),
                BottomNavigationBarItem(
                    icon: const Icon(Icons.hub),
                    label: text.pick('网络', 'Network')),
                BottomNavigationBarItem(
                    icon: const Icon(Icons.settings),
                    label: text.pick('设备', 'Device')),
              ],
            ),
          );
        },
      ),
    );
  }
}
