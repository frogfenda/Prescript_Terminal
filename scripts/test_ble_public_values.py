"""用已安装 NimBLE 1.4.x 的真实值容器/模板复核公开特征序列化，不使用字符串写入替身。"""

from pathlib import Path
import os
import re
import shutil
import subprocess


root = Path(__file__).resolve().parents[1]
dependency = root / ".pio/libdeps/esp32-s3/NimBLE-Arduino/src"
out = root / ".pio/host_tools/ble_public_values"
out.mkdir(parents=True, exist_ok=True)

# 仅替换平台头及临界区/日志设施；实际值容器、重载和模板实现全部来自安装依赖。
value_header = (dependency / "NimBLEAttValue.h").read_text(encoding="utf-8")
for include in ('#include "nimconfig.h"', '#include "NimBLELog.h"'):
    assert include in value_header
    value_header = value_header.replace(include, "")
(out / "host_att_value.h").write_text(value_header, encoding="utf-8")
characteristic_header = (dependency / "NimBLECharacteristic.h").read_text(encoding="utf-8")
forwarder = re.search(
    r"template<typename T>\s+void\s+setValue\(const T &s\) \{ m_value.setValue<T>\(s\); \}",
    characteristic_header,
)
assert forwarder, "NimBLE characteristic template changed; review this regression test"

# 从真实固件提取写入表达式，回退为单参数指针写入会在这里直接失败。
firmware = (root / "src/sys/sources/sys_ble.cpp").read_text(encoding="utf-8")
info_write = re.search(r"s_info_char->setValue\([^;]+;", firmware).group()
pair_write = re.search(r"s_pair_char->setValue\([^;]+;", firmware).group()
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#define CONFIG_BT_ENABLED 1
#define BLE_ATT_ATTR_MAX_LEN 512
#define NIMBLE_LOGE(...) ((void)0)
#define NIMBLE_LOGD(...) ((void)0)
inline void ble_npl_hw_enter_critical() {}
inline void ble_npl_hw_exit_critical(int) {}
#include "host_att_value.h"
struct Characteristic {
    NimBLEAttValue m_value;
    void setValue(const uint8_t* data, size_t size) { m_value.setValue(data, size); }
    FORWARDER
};
bool pairing_active;
bool PairingActive() { return pairing_active; }
int main() {
    Characteristic info, pair;
    auto* s_info_char = &info;
    auto* s_pair_char = &pair;
    std::string json = R"({"pairing_mode":true,"secure":false,"name":"终端"})";
    Characteristic legacy;
    legacy.setValue(json.c_str());
    assert(legacy.m_value.size() == sizeof(const char*));
    assert(std::string(reinterpret_cast<const char*>(legacy.m_value.data()), legacy.m_value.size()) != json);
    INFO_WRITE
    assert(info.m_value.size() == json.size());
    assert(std::memcmp(info.m_value.data(), json.data(), json.size()) == 0);
    for (bool active : {false, true, false}) {
        pairing_active = active;
        const bool pairing = PairingActive();
        PAIR_WRITE
        const std::string expected = active ? "PAIRING" : "IDLE";
        assert(pair.m_value.size() == expected.size());
        assert(std::memcmp(pair.m_value.data(), expected.data(), expected.size()) == 0);
    }
    std::cout << "NimBLE public values passed; legacy pointer payload=" << legacy.m_value.size() << " bytes\n";
}
'''
source = source.replace("FORWARDER", forwarder.group()).replace("INFO_WRITE", info_write).replace("PAIR_WRITE", pair_write)
(out / "test.cpp").write_text(source, encoding="utf-8")
compiler = shutil.which("g++") or r"C:\msys64\mingw64\bin\g++.exe"
env = os.environ.copy()
env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
executable = out / "test.exe"
subprocess.run([compiler, "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-finput-charset=UTF-8",
                str(out / "test.cpp"), "-o", str(executable)], env=env, check=True)
subprocess.run([str(executable)], env=env, check=True)
