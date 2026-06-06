import 'package:flutter/material.dart';

import '../../app/terminal_scope.dart';
import '../../core/i18n/app_text.dart';
import '../../core/theme/terminal_theme.dart';
import '../../widgets/status_chip.dart';
import '../../widgets/terminal_panel.dart';

class NetworkScreen extends StatelessWidget {
  const NetworkScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final state = terminal.state;
        final text = AppText(state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('身份', 'Identity'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      StatusChip(
                          label: text.userIdPending,
                          color: TerminalColors.muted),
                      StatusChip(
                          label: state.device?.deviceId ?? text.deviceIdPending,
                          color: TerminalColors.muted),
                      StatusChip(label: text.pick('本地蓝牙链路', 'Local BLE V1')),
                    ],
                  ),
                  const SizedBox(height: 10),
                  Text(
                    text.pick(
                      '当前版本只绑定当前蓝牙设备。后续接入账号后，这里会承载用户编号、设备编号、默认接收终端和远程授权。',
                      'V1 binds only the current Bluetooth device. After account support, this page will carry user ID, device ID, default receiver, and remote authorization.',
                    ),
                    style: const TextStyle(color: TerminalColors.muted),
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('云端档案连接', 'Cloud Archive Link'),
              child: Text(
                text.pick(
                  '云端档案：${terminal.cloudDirectives.length} 条已缓存\n来源：https://index.dimension-404.cloud/api/all',
                  'Cloud archive: ${terminal.cloudDirectives.length} cached\nSource: https://index.dimension-404.cloud/api/all',
                ),
                style: const TextStyle(color: TerminalColors.muted),
              ),
            ),
            TerminalPanel(
              title: text.pick('远程投递预留', 'Remote Delivery Reserved'),
              child: Text(
                text.pick(
                  '预留：给设备编号投递、给用户编号投递、收件箱、发件箱、云端队列、投递回执。\n当前所有命令仍通过本地蓝牙发往当前绑定设备。',
                  'Reserved: delivery to device ID, delivery to user ID, inbox, outbox, cloud queue, and delivery receipts.\nAll current commands still go through local Bluetooth to the bound device.',
                ),
                style: const TextStyle(color: TerminalColors.muted),
              ),
            ),
          ],
        );
      },
    );
  }
}
