import 'package:flutter/material.dart';

import '../../app/terminal_scope.dart';
import '../../core/i18n/app_text.dart';
import '../../core/theme/terminal_theme.dart';
import '../../widgets/status_chip.dart';
import '../../widgets/terminal_button.dart';
import '../../widgets/terminal_panel.dart';

class TerminalScreen extends StatelessWidget {
  const TerminalScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final controller = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: controller,
      builder: (context, _) {
        final state = controller.state;
        final text = AppText(state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('当前终端', 'Current Terminal'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    state.device?.displayName ?? text.unboundTerminal,
                    style: const TextStyle(
                        fontSize: 24,
                        color: TerminalColors.cyan,
                        fontWeight: FontWeight.w800),
                  ),
                  const SizedBox(height: 8),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      StatusChip(
                        label: text.statusLabel(state.connectionStatus),
                        color: state.isConnected
                            ? TerminalColors.green
                            : TerminalColors.red,
                      ),
                      StatusChip(label: text.languageLabel(state.language)),
                      StatusChip(label: text.modeLabel(state.languageMode)),
                      StatusChip(
                          label: state.device?.deviceId ?? text.deviceIdPending,
                          color: TerminalColors.muted),
                    ],
                  ),
                  const SizedBox(height: 12),
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      TerminalButton(
                        label:
                            state.isConnected ? text.disconnect : text.connect,
                        onPressed: () => state.isConnected
                            ? controller.disconnect()
                            : controller.connect(),
                      ),
                      TerminalButton(
                        label: text.sync,
                        onPressed: state.isConnected
                            ? () => controller.requestSync()
                            : null,
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  Text(_statusText(text, state.lastStatus),
                      style: const TextStyle(color: TerminalColors.muted)),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('设备快照', 'Device Snapshot'),
              child: Wrap(
                spacing: 12,
                runSpacing: 8,
                children: [
                  _Metric(
                      label: text.pick('闹钟', 'Alarms'),
                      value: '${state.alarms.length}'),
                  _Metric(
                      label: text.pick('日程', 'Schedules'),
                      value: '${state.schedules.length}'),
                  _Metric(
                      label: text.pick('指令池', 'Pool'),
                      value: '${state.prescripts.length}'),
                  _Metric(
                      label: text.pick('硬币预设', 'Coins'),
                      value: '${state.coins.length}'),
                  _Metric(
                      label: text.pick('特异点', 'Specials'),
                      value: '${state.specials.length}'),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('最近链路日志', 'Recent Link Log'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: state.logs.take(12).map((line) {
                  return Padding(
                    padding: const EdgeInsets.symmetric(vertical: 2),
                    child: Text(text.logLine(line),
                        style: const TextStyle(
                            color: TerminalColors.muted, fontSize: 12)),
                  );
                }).toList(),
              ),
            ),
          ],
        );
      },
    );
  }

  String _statusText(AppText text, String status) {
    if (!text.zh) return status;
    return status
        .replaceAll('SYS_STATUS: ONLINE', '系统状态：在线')
        .replaceAll('SYS_STATUS: OFFLINE', '系统状态：离线')
        .replaceAll('SYS_STATUS: CONNECTING', '系统状态：连接中')
        .replaceAll('ACK OK:', '确认：')
        .replaceAll('ACK WARN:', '警告：')
        .replaceAll('ACK ERR:', '错误：');
  }
}

class _Metric extends StatelessWidget {
  const _Metric({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 92,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(value,
              style: const TextStyle(
                  fontSize: 24,
                  color: TerminalColors.cyan,
                  fontWeight: FontWeight.w800)),
          Text(label,
              style:
                  const TextStyle(color: TerminalColors.muted, fontSize: 12)),
        ],
      ),
    );
  }
}
