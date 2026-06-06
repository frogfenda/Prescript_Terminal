import 'package:flutter/material.dart';

import '../../app/terminal_scope.dart';
import '../../core/i18n/app_text.dart';
import '../../core/protocol/terminal_protocol.dart';
import '../../core/theme/terminal_theme.dart';
import '../../widgets/terminal_button.dart';
import '../../widgets/terminal_list_tile.dart';
import '../../widgets/terminal_panel.dart';

class ScheduleScreen extends StatelessWidget {
  const ScheduleScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        return DefaultTabController(
          length: 4,
          child: Column(
            children: [
              TabBar(
                tabs: [
                  Tab(text: text.pick('硬币', 'Coins')),
                  Tab(text: text.pick('闹钟', 'Alarms')),
                  Tab(text: text.pick('日程', 'Schedule')),
                  Tab(text: text.pick('专注', 'Focus')),
                ],
              ),
              const Expanded(
                child: TabBarView(
                  children: [
                    CoinPresetTab(),
                    AlarmTab(),
                    ScheduleTab(),
                    PomodoroTab(),
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class CoinPresetTab extends StatefulWidget {
  const CoinPresetTab({super.key});

  @override
  State<CoinPresetTab> createState() => _CoinPresetTabState();
}

class _CoinPresetTabState extends State<CoinPresetTab> {
  final nameController = TextEditingController(text: '肆意劈砍');
  final baseController = TextEditingController(text: '4');
  final coinController = TextEditingController(text: '5');
  int count = 3;
  List<int> colors = [0, 0, 0];

  @override
  void dispose() {
    nameController.dispose();
    baseController.dispose();
    coinController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('硬币预设编辑器', 'Coin Preset Editor'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TextField(
                      controller: nameController,
                      decoration: InputDecoration(
                          labelText: text.pick('技能名', 'Skill name'))),
                  const SizedBox(height: 8),
                  Row(
                    children: [
                      Expanded(
                        child: TextField(
                          controller: baseController,
                          keyboardType: TextInputType.number,
                          decoration: InputDecoration(
                              labelText: text.pick('基础点数', 'Base power')),
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: TextField(
                          controller: coinController,
                          keyboardType: TextInputType.number,
                          decoration: InputDecoration(
                              labelText: text.pick('硬币点数', 'Coin power')),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  DropdownButtonFormField<int>(
                    initialValue: count,
                    decoration: InputDecoration(
                        labelText: text.pick('硬币数量', 'Coin count')),
                    items: List.generate(
                        18,
                        (i) => DropdownMenuItem(
                            value: i + 1, child: Text('${i + 1}'))),
                    onChanged: (value) {
                      if (value == null) return;
                      setState(() {
                        count = value;
                        colors = List.generate(
                            count, (i) => i < colors.length ? colors[i] : 0);
                      });
                    },
                  ),
                  const SizedBox(height: 8),
                  ...List.generate(count, (index) {
                    return Padding(
                      padding: const EdgeInsets.only(bottom: 6),
                      child: DropdownButtonFormField<int>(
                        initialValue: colors[index],
                        decoration: InputDecoration(
                            labelText: text.pick('硬币 ${index + 1} 材质',
                                'Coin ${index + 1} material')),
                        items: [
                          DropdownMenuItem(
                              value: 0,
                              child: Text(text.pick('经典金', 'Classic gold'))),
                          DropdownMenuItem(
                              value: 1,
                              child: Text(text.pick('狂气红', 'Frenzy red'))),
                          DropdownMenuItem(
                              value: 2,
                              child: Text(text.pick('沉稳绿', 'Steady green'))),
                        ],
                        onChanged: (value) =>
                            setState(() => colors[index] = value ?? 0),
                      ),
                    );
                  }),
                  TerminalButton(
                    label: text.overwriteAndSync,
                    onPressed: terminal.state.isConnected
                        ? () => terminal.sendRaw(
                              TerminalCommands.addCoin(
                                basePower:
                                    int.tryParse(baseController.text) ?? 0,
                                coinPower:
                                    int.tryParse(coinController.text) ?? 0,
                                count: count,
                                colors: colors.join(),
                                name: nameController.text,
                              ),
                              syncAfter: true,
                            )
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('已同步硬币预设', 'Synced Coin Presets'),
              child: Column(
                children: terminal.state.coins.isEmpty
                    ? [
                        Text(
                            text.pick('终端目前没有硬币预设。',
                                'No coin presets on the terminal.'),
                            style: const TextStyle(color: TerminalColors.muted))
                      ]
                    : terminal.state.coins.map((coin) {
                        return TerminalListTile(
                          title:
                              '[${coin.name}] ${text.pick('基础', 'Base')} ${coin.basePower} / ${text.pick('硬币', 'Coin')} ${coin.coinPower}',
                          subtitle:
                              '${text.pick('数量', 'Count')} ${coin.count} | ${text.pick('材质', 'Colors')} ${coin.colors}',
                          trailing: TerminalButton(
                            label: text.delete,
                            destructive: true,
                            compact: true,
                            onPressed: terminal.state.isConnected
                                ? () => terminal.sendRaw(
                                    TerminalCommands.deleteCoin(coin.name),
                                    syncAfter: true)
                                : null,
                          ),
                        );
                      }).toList(),
              ),
            ),
          ],
        );
      },
    );
  }
}

class AlarmTab extends StatefulWidget {
  const AlarmTab({super.key});

  @override
  State<AlarmTab> createState() => _AlarmTabState();
}

class _AlarmTabState extends State<AlarmTab> {
  TimeOfDay time = const TimeOfDay(hour: 8, minute: 0);
  final nameController = TextEditingController(text: '早间唤醒');
  final textController = TextEditingController(text: '致...沉睡者...立刻清醒');

  @override
  void dispose() {
    nameController.dispose();
    textController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('闹钟矩阵', 'Alarm Matrix'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TerminalButton(
                    label:
                        '${text.pick('时间', 'Time')} ${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}',
                    onPressed: () async {
                      final picked = await showTimePicker(
                          context: context, initialTime: time);
                      if (picked != null) setState(() => time = picked);
                    },
                  ),
                  const SizedBox(height: 8),
                  TextField(
                      controller: nameController,
                      decoration: InputDecoration(
                          labelText: text.pick('代号', 'Code name'))),
                  const SizedBox(height: 8),
                  TextField(
                      controller: textController,
                      decoration: InputDecoration(
                          labelText: text.pick('指令', 'Directive'))),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.addAndSync,
                    onPressed: terminal.state.isConnected
                        ? () => terminal.sendRaw(
                              TerminalCommands.addAlarm(
                                hour: time.hour,
                                minute: time.minute,
                                name: nameController.text,
                                text: textController.text,
                              ),
                              syncAfter: true,
                            )
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('已同步闹钟', 'Synced Alarms'),
              child: Column(
                children: terminal.state.alarms.isEmpty
                    ? [
                        Text(
                            text.pick(
                                '终端目前没有闹钟。', 'No alarms on the terminal.'),
                            style: const TextStyle(color: TerminalColors.muted))
                      ]
                    : terminal.state.alarms.map((alarm) {
                        return TerminalListTile(
                          title:
                              '${alarm.hour.toString().padLeft(2, '0')}:${alarm.minute.toString().padLeft(2, '0')} ${alarm.name}',
                          subtitle: alarm.text,
                          trailing: TerminalButton(
                            label: text.delete,
                            destructive: true,
                            compact: true,
                            onPressed: terminal.state.isConnected
                                ? () => terminal.sendRaw(
                                    TerminalCommands.deleteAlarm(alarm.name),
                                    syncAfter: true)
                                : null,
                          ),
                        );
                      }).toList(),
              ),
            ),
          ],
        );
      },
    );
  }
}

class ScheduleTab extends StatefulWidget {
  const ScheduleTab({super.key});

  @override
  State<ScheduleTab> createState() => _ScheduleTabState();
}

class _ScheduleTabState extends State<ScheduleTab> {
  DateTime target = DateTime.now().add(const Duration(hours: 1));
  final titleController = TextEditingController(text: '开会');
  final textController = TextEditingController(text: '致...大忙人...在十五分钟后到会议室');

  @override
  void dispose() {
    titleController.dispose();
    textController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('日程登记', 'Schedule Registry'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TerminalButton(
                    label: _formatDateTime(target),
                    onPressed: () async {
                      final date = await showDatePicker(
                        context: context,
                        firstDate: DateTime.now(),
                        lastDate: DateTime(2099),
                        initialDate: target,
                      );
                      if (!context.mounted || date == null) return;
                      final time = await showTimePicker(
                          context: context,
                          initialTime: TimeOfDay.fromDateTime(target));
                      if (time != null) {
                        setState(() {
                          target = DateTime(date.year, date.month, date.day,
                              time.hour, time.minute);
                        });
                      }
                    },
                  ),
                  const SizedBox(height: 8),
                  TextField(
                      controller: titleController,
                      decoration:
                          InputDecoration(labelText: text.pick('标题', 'Title'))),
                  const SizedBox(height: 8),
                  TextField(
                      controller: textController,
                      decoration: InputDecoration(
                          labelText: text.pick('内容', 'Content'))),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.saveAndSync,
                    onPressed: terminal.state.isConnected
                        ? () => terminal.sendRaw(
                              TerminalCommands.addSchedule(
                                dateTime: target,
                                title: titleController.text,
                                text: textController.text,
                              ),
                              syncAfter: true,
                            )
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('生效日程', 'Active Schedules'),
              child: Column(
                children: terminal.state.schedules.isEmpty
                    ? [
                        Text(
                            text.pick('终端目前没有未过期日程。',
                                'No active schedules on the terminal.'),
                            style: const TextStyle(color: TerminalColors.muted))
                      ]
                    : terminal.state.schedules.map((schedule) {
                        return TerminalListTile(
                          title:
                              '${schedule.dateTime.replaceAll('T', ' ')} ${schedule.name}',
                          subtitle: schedule.text,
                          trailing: TerminalButton(
                            label: text.delete,
                            destructive: true,
                            compact: true,
                            onPressed: terminal.state.isConnected
                                ? () => terminal.sendRaw(
                                    TerminalCommands.deleteSchedule(
                                        schedule.name),
                                    syncAfter: true)
                                : null,
                          ),
                        );
                      }).toList(),
              ),
            ),
          ],
        );
      },
    );
  }

  String _formatDateTime(DateTime value) {
    String two(int v) => v.toString().padLeft(2, '0');
    return '${value.year}-${two(value.month)}-${two(value.day)} ${two(value.hour)}:${two(value.minute)}';
  }
}

class PomodoroTab extends StatefulWidget {
  const PomodoroTab({super.key});

  @override
  State<PomodoroTab> createState() => _PomodoroTabState();
}

class _PomodoroTabState extends State<PomodoroTab> {
  int slot = 0;
  final nameController = TextEditingController(text: '极限心流模式');
  final workController = TextEditingController(text: '45');
  final restController = TextEditingController(text: '10');

  @override
  void dispose() {
    nameController.dispose();
    workController.dispose();
    restController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('专注预设', 'Focus Preset'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  DropdownButtonFormField<int>(
                    initialValue: slot,
                    decoration:
                        InputDecoration(labelText: text.pick('槽位', 'Slot')),
                    items: [
                      DropdownMenuItem(
                          value: 0, child: Text(text.pick('槽位 0', 'Slot 0'))),
                      DropdownMenuItem(
                          value: 1, child: Text(text.pick('槽位 1', 'Slot 1'))),
                      DropdownMenuItem(
                          value: 2, child: Text(text.pick('槽位 2', 'Slot 2'))),
                    ],
                    onChanged: (value) => setState(() => slot = value ?? 0),
                  ),
                  const SizedBox(height: 8),
                  TextField(
                      controller: nameController,
                      decoration:
                          InputDecoration(labelText: text.pick('名称', 'Name'))),
                  const SizedBox(height: 8),
                  TextField(
                      controller: workController,
                      keyboardType: TextInputType.number,
                      decoration: InputDecoration(
                          labelText: text.pick('专注分钟', 'Work minutes'))),
                  const SizedBox(height: 8),
                  TextField(
                      controller: restController,
                      keyboardType: TextInputType.number,
                      decoration: InputDecoration(
                          labelText: text.pick('休息分钟', 'Rest minutes'))),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.overwriteAndSync,
                    onPressed: terminal.state.isConnected
                        ? () => terminal.sendRaw(
                              TerminalCommands.updatePomodoro(
                                slot: slot,
                                name: nameController.text,
                                workMinutes:
                                    int.tryParse(workController.text) ?? 25,
                                restMinutes:
                                    int.tryParse(restController.text) ?? 5,
                              ),
                              syncAfter: true,
                            )
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('未来时间线', 'Future Timeline'),
              child: Text(
                text.pick(
                  '预留：隐藏日程、远程投递计划、周期指令、云端待投递队列。',
                  'Reserved: hidden schedules, remote delivery plans, recurring directives, and pending cloud queue.',
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
