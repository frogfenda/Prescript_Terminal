import 'package:flutter/material.dart';

import '../../app/terminal_scope.dart';
import '../../core/i18n/app_text.dart';
import '../../core/models/terminal_records.dart';
import '../../core/protocol/terminal_protocol.dart';
import '../../core/theme/terminal_theme.dart';
import '../../widgets/terminal_button.dart';
import '../../widgets/terminal_list_tile.dart';
import '../../widgets/terminal_panel.dart';

class DirectivesScreen extends StatelessWidget {
  const DirectivesScreen({super.key});

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
                  Tab(text: text.pick('发送', 'Send')),
                  Tab(text: text.pick('浏览', 'Browse')),
                  Tab(text: text.pick('投稿', 'Submit')),
                  Tab(text: text.pick('特异点', 'Special')),
                ],
              ),
              const Expanded(
                child: TabBarView(
                  children: [
                    DirectiveSendTab(),
                    DirectiveBrowseTab(),
                    DirectiveSubmitTab(),
                    SpecialDirectiveTab(),
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

class DirectiveSendTab extends StatefulWidget {
  const DirectiveSendTab({super.key});

  @override
  State<DirectiveSendTab> createState() => _DirectiveSendTabState();
}

class _DirectiveSendTabState extends State<DirectiveSendTab> {
  final txtController = TextEditingController(text: '致...终止交易。');
  final preController = TextEditingController();
  final targetController = TextEditingController();

  @override
  void dispose() {
    txtController.dispose();
    preController.dispose();
    targetController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final connected = terminal.state.isConnected;
        final text = AppText(terminal.state.language);
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('使用者', 'User'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      Text(
                        terminal.state.currentPrescriptTarget.isEmpty
                            ? text.pick('当前使用者：未设置', 'Current user: none')
                            : text.pick(
                                '当前使用者：${terminal.state.currentPrescriptTarget}',
                                'Current user: ${terminal.state.currentPrescriptTarget}',
                              ),
                        style: const TextStyle(color: TerminalColors.muted),
                      ),
                      TerminalButton(
                        label: text.pick('清空', 'Clear'),
                        compact: true,
                        onPressed: connected
                            ? () => terminal.setPrescriptTarget('')
                            : null,
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  TextField(
                    controller: targetController,
                    decoration: InputDecoration(
                        labelText: text.pick('新增使用者', 'New user')),
                  ),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.pick('新增并选中', 'Add and select'),
                    onPressed: connected
                        ? () =>
                            terminal.addPrescriptTarget(targetController.text)
                        : null,
                  ),
                  const SizedBox(height: 8),
                  if (terminal.state.prescriptTargets.isEmpty)
                    Text(
                      text.pick('还没有使用者。新增后，设备会把指令开头的“致...”替换为该使用者。',
                          'No users yet. After adding one, the terminal replaces the leading user marker.'),
                      style: const TextStyle(color: TerminalColors.muted),
                    )
                  else
                    ...terminal.state.prescriptTargets.map((target) {
                      final selected =
                          target == terminal.state.currentPrescriptTarget;
                      return TerminalListTile(
                        title: target,
                        subtitle: selected
                            ? text.pick('当前选中', 'Selected')
                            : text.pick('点按设为当前使用者', 'Tap to select user'),
                        onTap: connected
                            ? () => terminal.setPrescriptTarget(target)
                            : null,
                        trailing: TerminalButton(
                          label: text.delete,
                          destructive: true,
                          compact: true,
                          onPressed: connected
                              ? () => terminal.deletePrescriptTarget(target)
                              : null,
                        ),
                      );
                    }),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('即时指令', 'Instant Directive'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TextField(
                      controller: txtController,
                      decoration: InputDecoration(
                          labelText: text.pick('指令内容', 'Content'))),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.pick('发送到终端', 'Send to terminal'),
                    onPressed: connected
                        ? () => terminal.sendRaw(
                            TerminalCommands.textNotify(txtController.text),
                            syncAfter: false)
                        : null,
                  ),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('指令池注入', 'Prescript Pool'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TextField(
                      controller: preController,
                      decoration: InputDecoration(
                          labelText: text.pick('新指令文本', 'New directive'))),
                  const SizedBox(height: 8),
                  TerminalButton(
                    label: text.pick(
                        '注入当前语言指令池', 'Inject into current language pool'),
                    onPressed: connected
                        ? () => terminal.sendRaw(
                              TerminalCommands.addPrescript(
                                  terminal.state.language, preController.text),
                              syncAfter: true,
                            )
                        : null,
                  ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class DirectiveBrowseTab extends StatelessWidget {
  const DirectiveBrowseTab({super.key});

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
              title: text.pick('云端档案', 'Cloud Archive'),
              action: TerminalButton(
                  label: text.refresh,
                  compact: true,
                  onPressed: terminal.loadCloudArchive),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  TerminalButton(
                    label: text.pick('随机注入 10 条', 'Inject 10 random items'),
                    onPressed: terminal.state.isConnected
                        ? () => terminal.injectRandomCloudDirectives()
                        : null,
                  ),
                  const SizedBox(height: 8),
                  ...terminal.cloudDirectives.take(50).map((item) {
                    final uploaded =
                        terminal.uploadedItems.contains(item.content);
                    return TerminalListTile(
                      title: item.content,
                      subtitle: '${text.pick('提交者', 'Author')}: ${item.author}',
                      trailing: TerminalButton(
                        label: uploaded ? text.delete : text.inject,
                        destructive: uploaded,
                        compact: true,
                        onPressed: terminal.state.isConnected
                            ? () => uploaded
                                ? terminal.removeCloudDirective(item)
                                : terminal.injectCloudDirective(item)
                            : null,
                      ),
                    );
                  }),
                ],
              ),
            ),
            TerminalPanel(
              title: text.pick('本机指令池', 'Local Prescript Pool'),
              child: Column(
                children: terminal.state.prescripts.isEmpty
                    ? [
                        Text(
                            text.pick('等待设备同步指令池数据。',
                                'Waiting for prescript pool sync.'),
                            style: const TextStyle(color: TerminalColors.muted))
                      ]
                    : terminal.state.prescripts.map((record) {
                        return TerminalListTile(
                          title: record.text,
                          trailing: TerminalButton(
                            label: text.delete,
                            destructive: true,
                            compact: true,
                            onPressed: terminal.state.isConnected
                                ? () => terminal.sendRaw(
                                      TerminalCommands.deletePrescript(
                                          terminal.state.language, record.text),
                                      syncAfter: true,
                                    )
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

class DirectiveSubmitTab extends StatelessWidget {
  const DirectiveSubmitTab({super.key});

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
              title: text.pick('投稿入口', 'Submission Gate'),
              child: Text(
                text.pick(
                  '当前预留投稿入口。\n后续可接入账号、投稿历史、审核状态、被接收次数与远程投递队列。',
                  'Submission entry reserved.\nFuture work can include account, history, review state, acceptance count, and remote delivery queue.',
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

class SpecialDirectiveTab extends StatelessWidget {
  const SpecialDirectiveTab({super.key});

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    return AnimatedBuilder(
      animation: terminal,
      builder: (context, _) {
        final text = AppText(terminal.state.language);
        final specials = terminal.state.specials.values.toList();
        return ListView(
          children: [
            TerminalPanel(
              title: text.pick('特异点数据库', 'Special Directive Database'),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: specials.isEmpty
                    ? [
                        Text(
                          text.pick('连接并同步后显示人物链条与特殊指令。',
                              'Connect and sync to show character links and special directives.'),
                          style: const TextStyle(color: TerminalColors.muted),
                        ),
                      ]
                    : specials
                        .map((record) =>
                            _SpecialTile(record: record, text: text))
                        .toList(),
              ),
            ),
          ],
        );
      },
    );
  }
}

class _SpecialTile extends StatelessWidget {
  const _SpecialTile({required this.record, required this.text});

  final SpecialRecord record;
  final AppText text;

  @override
  Widget build(BuildContext context) {
    final terminal = TerminalScope.of(context);
    final kind = record.kind == 'C'
        ? text.pick('人物链条', 'Character link')
        : text.pick('异想体', 'Abnormality');
    return TerminalListTile(
      title: '${record.name} / ${record.id}',
      subtitle:
          '$kind | ${text.pick('概率', 'Probability')} ${record.probability} | ${text.pick('进度', 'Progress')} ${record.progress}\n'
          '${text.pick('弹窗', 'Popup')} ${record.popupTitle}'
          '${record.fetchedText == null ? '' : '\n${record.fetchedText}'}',
      trailing: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          TerminalButton(
            label: text.trigger,
            compact: true,
            onPressed: terminal.state.isConnected
                ? () => terminal.sendRaw(
                    TerminalCommands.forceSpecial(record.id),
                    syncAfter: false)
                : null,
          ),
          const SizedBox(height: 4),
          TerminalButton(
            label: text.read,
            compact: true,
            onPressed: terminal.state.isConnected
                ? () => terminal.sendRaw(
                    TerminalCommands.getSpecialText(record.id),
                    syncAfter: false)
                : null,
          ),
        ],
      ),
    );
  }
}
