/*
【模块职责】提供隔离的 W25N01GV 串口诊断入口，验证身份、状态、ECC、坏块标记、BBM LUT
以及一个明确保留块的擦写能力。
【调用关系】仅在 esp32-s3-nand-test 环境中由 SysBootTest 调用；正式固件不会进入本模块。
【安全约束】启动只做读取。只有串口收到完整确认命令后才允许测试999号块，测试不会修改BBM LUT或OTP。
*/
#pragma once

namespace SysNandDiagnostic
{
    /** 初始化 NAND 并自动执行一次只读诊断，随后在串口等待命令。 */
    void Setup();

    /** 消费串口命令；调用者应在隔离测试固件主循环中持续调用。 */
    void Loop();
}
