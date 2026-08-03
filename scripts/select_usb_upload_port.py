"""
【脚本职责】为PlatformIO选择V4B的USB串口，并在1200bps切换后追踪同一块板的ROM串口。
【调用时机】作为PlatformIO post extra script加载；只在upload目标中修改端口选择。
【重要约束】应用态是303A:0002，ROM USB-Serial/JTAG是303A:1001；两者的序列号
只有分隔符形式不同。不按COM号硬编码，不将蓝牙虚拟串口交给esptool。
"""

import re
import time

from SCons.Script import COMMAND_LINE_TARGETS
from serial import Serial, SerialException
from serial.tools.list_ports import comports

Import("env")


ESPRESSIF_USB_VID = 0x303A
APP_TINYUSB_PID = 0x0002
ROM_USB_JTAG_PID = 0x1001
ROM_ENUMERATION_TIMEOUT_SECONDS = 15.0
PORT_READY_TIMEOUT_SECONDS = 3.0
POLL_INTERVAL_SECONDS = 0.20


def normalize_serial(value):
    """将TinyUSB的紧凑MAC与ROM的冒号MAC统一为只含字母数字的大写串。"""
    return re.sub(r"[^0-9A-Za-z]", "", value or "").upper()


def supported_ports():
    """只返回项目实际使用的两种Espressif USB身份，从源头排除蓝牙COM口。"""
    return [
        port
        for port in comports()
        if port.vid == ESPRESSIF_USB_VID
        and port.pid in (APP_TINYUSB_PID, ROM_USB_JTAG_PID)
    ]


def port_by_name(device):
    """查找显式--upload-port对应的当前串口，用于继续记录设备身份。"""
    wanted = (device or "").upper()
    for port in comports():
        if port.device.upper() == wanted:
            return port
    return None


def describe(port):
    """生成面向上传终端的稳定设备描述，不依赖Windows本地化的设备名称。"""
    serial = normalize_serial(port.serial_number) or "无序列号"
    return "%s (VID=%04X, PID=%04X, 序列号=%s)" % (
        port.device,
        port.vid,
        port.pid,
        serial,
    )


def wait_until_openable(device, deadline):
    """
    Windows可能先创建COM名称，稍后才完成驱动初始化。
    在限时内做一次与PlatformIO原实现相同的打开/关闭检查，避免esptool紧接着收到“信号灯超时”。
    """
    while time.monotonic() < deadline:
        try:
            serial_port = Serial(device, baudrate=115200, timeout=0.1)
            serial_port.close()
            return True
        except (SerialException, OSError):
            time.sleep(POLL_INTERVAL_SECONDS)
    return False


def choose_initial_port(build_env):
    """
    优先使用用户显式指定的端口；未指定时优先选应用态，其次选ROM态。
    同一身份出现多块板时不猜测，要求使用--upload-port明确指定。
    """
    configured = build_env.subst("$UPLOAD_PORT").strip()
    if configured and configured != "$UPLOAD_PORT":
        selected = port_by_name(configured)
        if selected:
            print("[上传] 使用显式指定的串口：%s" % describe(selected))
        return selected

    ports = supported_ports()
    application_ports = [port for port in ports if port.pid == APP_TINYUSB_PID]
    rom_ports = [port for port in ports if port.pid == ROM_USB_JTAG_PID]
    candidates = application_ports or rom_ports

    if not candidates:
        print(
            "[上传] 未找到V4B USB串口（303A:0002或303A:1001）；"
            "已拒绝选择蓝牙COM口。请复位或重新连接设备。"
        )
        build_env.Exit(1)

    if len(candidates) > 1:
        print("[上传] 检测到多块候选设备，为避免烧错板已停止：")
        for port in candidates:
            print("  - %s" % describe(port))
        print("[上传] 请用--upload-port COMx明确指定。")
        build_env.Exit(1)

    selected = candidates[0]
    build_env.Replace(UPLOAD_PORT=selected.device)
    identity = "应用态" if selected.pid == APP_TINYUSB_PID else "ROM态"
    print("[上传] 已自动选择%s串口：%s" % (identity, describe(selected)))
    return selected


def install_same_device_wait(build_env, selected):
    """
    替换PlatformIO默认的“第一个新COM口”策略。
    应用态必须切换到序列号或物理路径相同的ROM态；ROM态固件则继续使用原端口。
    """
    if not selected or selected.vid != ESPRESSIF_USB_VID or selected.pid not in (
        APP_TINYUSB_PID,
        ROM_USB_JTAG_PID,
    ):
        return

    selected_serial = normalize_serial(selected.serial_number)
    selected_location = selected.location or ""
    selected_pid = selected.pid
    previous_device = selected.device.upper()

    def belongs_to_selected_board(port):
        current_serial = normalize_serial(port.serial_number)
        if selected_serial and current_serial:
            return selected_serial == current_serial
        if selected_location and port.location:
            return selected_location == port.location
        return False

    def wait_for_same_device(build_env, before):
        del before  # 不使用PlatformIO的全端口快照，因为其中包含蓝牙COM口。

        if selected_pid == ROM_USB_JTAG_PID:
            print("[上传] 设备已在ROM USB-Serial/JTAG模式，继续使用%s。" % selected.device)
            deadline = time.monotonic() + PORT_READY_TIMEOUT_SECONDS
            if wait_until_openable(selected.device, deadline):
                return selected.device
            print("[上传] ROM串口%s存在但未就绪。" % selected.device)
            build_env.Exit(1)

        print("[上传] 正在等待同一块V4B切换到ROM USB-Serial/JTAG……")
        deadline = time.monotonic() + ROM_ENUMERATION_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            for port in supported_ports():
                if port.pid != ROM_USB_JTAG_PID:
                    continue
                if port.device.upper() == previous_device:
                    continue
                if not belongs_to_selected_board(port):
                    continue

                ready_deadline = min(
                    deadline,
                    time.monotonic() + PORT_READY_TIMEOUT_SECONDS,
                )
                if wait_until_openable(port.device, ready_deadline):
                    print("[上传] 已匹配同一设备的ROM串口：%s" % describe(port))
                    return port.device
            time.sleep(POLL_INTERVAL_SECONDS)

        print(
            "[上传] 15秒内未找到同一设备的ROM串口；"
            "未回退到蓝牙串口或其他ESP32。"
        )
        build_env.Exit(1)

    # post extra script在平台builder之后加载；BeforeUpload执行时会动态调用这个方法。
    build_env.AddMethod(wait_for_same_device, "WaitForNewSerialPort")


def configure_upload_port_tracking():
    """普通Build不枚举串口，只有upload目标才安装端口选择与追踪逻辑。"""
    if "upload" not in COMMAND_LINE_TARGETS:
        return
    selected = choose_initial_port(env)
    install_same_device_wait(env, selected)


configure_upload_port_tracking()
