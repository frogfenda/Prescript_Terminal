// 文件：src/net/includes/net/net_builtin_tasks.h
/* 【模块职责】集中安装固件内置网络任务，避免 NetService 依赖具体业务模块。 */
#pragma once

/** 注册全部内置网络任务；单项失败会记录日志，不阻止其他任务注册。 */
void NetBuiltinTasks_RegisterAll();
