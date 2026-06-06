import 'package:flutter/material.dart';

import '../../app/terminal_scope.dart';
import '../../core/i18n/app_text.dart';
import '../../core/protocol/terminal_protocol.dart';
import '../../core/theme/terminal_theme.dart';
import '../../widgets/terminal_button.dart';
import '../../widgets/terminal_panel.dart';

class DeviceScreen extends StatelessWidget {
  const DeviceScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return const HardwareTab();
  }
}

class HardwareTab extends StatefulWidget {
  const HardwareTab({super.key});

  @override
  State<HardwareTab> createState() => _HardwareTabState();
}

class _HardwareTabState extends State<HardwareTab> {
  final ssidController = TextEditingController();
  final passwordController = TextEditingController();

  @override
  void dispose() {
    ssidController.dispose();
    passwordController.dispose();
    super.dispose();
  }

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
              title: text.pick('蓝牙链路', 'Bluetooth Link'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                      '${text.pick('设备名', 'Device name')}：${state.device?.bleName ?? terminalBleName}',
                      style: const TextStyle(color: TerminalColors.muted)),
                  Text(
                      '${text.pick('语言', 'Language')}：${text.languageLabel(state.language)} / ${text.modeLabel(state.languageMode)}',
                      style: const TextStyle(color: TerminalColors.muted)),
                  Text(
                      '${text.pick('固件', 'Firmware')}：${state.buildCode.isEmpty ? text.unknown : state.buildCode}',
                      style: const TextStyle(color: TerminalColors.muted)),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: state.isConnected
                        ? text.pick('断开蓝牙', 'Disconnect Bluetooth')
                        : text.pick('连接蓝牙', 'Connect Bluetooth'),
                    onPressed: () => state.isConnected
                        ? terminal.disconnect()
                        : terminal.connect(),
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('无线网络配置', 'Wireless Network Setup'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TextField(
                      controller: ssidController,
                      decoration: InputDecoration(
                          labelText: text.pick('网络名称', 'SSID'))),
                  const SizedBox(height: 8),
                  TextField(
                    controller: passwordController,
                    obscureText: true,
                    decoration:
                        InputDecoration(labelText: text.pick('密码', 'Password')),
                  ),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.pick('下发无线网络密钥', 'Send WiFi credentials'),
                    onPressed: state.isConnected
                        ? () => terminal.sendRaw(
                              TerminalCommands.setWifi(
                                  ssidController.text, passwordController.text),
                              syncAfter: false,
                            )
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('预留硬件设置', 'Reserved Hardware Settings'),
              child: Text(
                text.pick(
                  '预留：音量、震动、屏幕动画、近场通信模式、自动推送、固件升级、设备重命名、解绑设备。',
                  'Reserved: volume, haptics, screen animation, NFC mode, auto push, firmware update, rename, and unbind.',
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
