/*
【模块职责】设备身份凭据存储实现。把一条完整身份记录作为单个 NVS Blob 原子更新，避免普通 config.json 重写或文件系统更新误伤凭据。
【存储边界】identity 分区独立于默认 NVS、LittleFS、FATFS 与 OTA；当前仅提供本地初始化、首次绑定和安全查询，不执行任何联网动作。
【安全边界】NVS 提供掉电保护与磨损均衡，但未启用 Flash Encryption 时不等同于物理防提取；量产安全启动阶段需另行启用加密。
*/
#include "sys/sys_device_identity.h"

#include <ArduinoJson.h>
#include <cstddef>
#include <cstring>
#include <esp_err.h>
#include <esp_mac.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace
{
    constexpr char kPartitionLabel[] = "identity";
    constexpr char kNamespace[] = "device";
    constexpr char kRecordKey[] = "identity";
    constexpr uint32_t kRecordMagic = 0x50544944UL;
    constexpr uint16_t kSchemaVersion = 1;
    constexpr size_t kMaxPublicIdLength = 64;
    constexpr size_t kMinDeviceKeyLength = 32;
    constexpr size_t kMaxDeviceKeyLength = 128;

    /*
     * 固定长度记录只占两百余字节。所有身份字段放在同一个 Blob 中，NVS 对单键更新的原子性
     * 能避免掉电时出现“公开 ID 已更新但通行码未更新”的半写入状态。
     */
    struct __attribute__((packed)) DeviceIdentityRecord
    {
        uint32_t magic;
        uint16_t schema_version;
        uint16_t public_id_length;
        uint16_t device_key_length;
        uint16_t reserved;
        uint32_t credential_version;
        char public_id[kMaxPublicIdLength + 1];
        char device_key[kMaxDeviceKeyLength + 1];
        uint32_t checksum;
    };

    static_assert(sizeof(DeviceIdentityRecord) == 214, "身份记录布局变化时必须提升 schema_version");

    bool s_storageReady = false;
    bool s_provisioned = false;
    String s_machineCode;
    DeviceIdentityRecord s_record = {};

    // 【函数说明】计算轻量完整性校验，仅用于发现意外损坏；它不是密码学签名。
    uint32_t CalculateChecksum(const DeviceIdentityRecord &record)
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
        const size_t length = offsetof(DeviceIdentityRecord, checksum);
        uint32_t hash = 2166136261UL;
        for (size_t i = 0; i < length; ++i)
        {
            hash ^= bytes[i];
            hash *= 16777619UL;
        }
        return hash;
    }

    // 【函数说明】公开 ID 只接受适合文本协议与 URL 路径的 ASCII 字符，避免冒号、竖线和换行破坏命令边界。
    bool IsPublicIdValid(const String &value)
    {
        if (value.length() == 0 || value.length() > kMaxPublicIdLength)
            return false;

        for (size_t i = 0; i < value.length(); ++i)
        {
            const char ch = value.charAt(i);
            const bool alphaNumeric = (ch >= '0' && ch <= '9') ||
                                      (ch >= 'A' && ch <= 'Z') ||
                                      (ch >= 'a' && ch <= 'z');
            if (!alphaNumeric && ch != '-' && ch != '_' && ch != '.')
                return false;
        }
        return true;
    }

    // 【函数说明】当前服务器使用 URL-safe 随机通行码；限制字符集也能保证 BLE 宏协议不会被载荷截断。
    bool IsDeviceKeyValid(const String &value)
    {
        if (value.length() < kMinDeviceKeyLength || value.length() > kMaxDeviceKeyLength)
            return false;

        for (size_t i = 0; i < value.length(); ++i)
        {
            const char ch = value.charAt(i);
            const bool alphaNumeric = (ch >= '0' && ch <= '9') ||
                                      (ch >= 'A' && ch <= 'Z') ||
                                      (ch >= 'a' && ch <= 'z');
            if (!alphaNumeric && ch != '-' && ch != '_')
                return false;
        }
        return true;
    }

    // 【函数说明】从只读 eFuse 基础 MAC 生成稳定机器码；不把可变 Wi-Fi/BLE 接口地址当作设备身份。
    String BuildMachineCode()
    {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) != ESP_OK)
            return String();

        char output[20] = {};
        snprintf(output,
                 sizeof(output),
                 "PT-%02X%02X%02X%02X%02X%02X",
                 mac[0],
                 mac[1],
                 mac[2],
                 mac[3],
                 mac[4],
                 mac[5]);
        return String(output);
    }

    // 【函数说明】严格验证长度、结尾空字符、字符集和校验值；损坏记录不会被当成“未绑定”后静默覆盖。
    bool IsRecordValid(const DeviceIdentityRecord &record)
    {
        if (record.magic != kRecordMagic || record.schema_version != kSchemaVersion)
            return false;
        if (record.public_id_length == 0 || record.public_id_length > kMaxPublicIdLength)
            return false;
        if (record.device_key_length < kMinDeviceKeyLength || record.device_key_length > kMaxDeviceKeyLength)
            return false;
        if (record.public_id[record.public_id_length] != '\0' || record.device_key[record.device_key_length] != '\0')
            return false;
        if (record.credential_version == 0 || record.checksum != CalculateChecksum(record))
            return false;
        return IsPublicIdValid(String(record.public_id)) && IsDeviceKeyValid(String(record.device_key));
    }

    /*
     * 【函数说明】初始化自定义 NVS。第一次改用新分区表时，该区域可能残留旧 LittleFS 尾部数据；
     * 仅在 NVS 明确报告格式不可用时擦除 identity 分区，绝不触碰默认 NVS、LittleFS 或 FATFS。
     */
    bool PrepareIdentityPartition()
    {
        esp_err_t error = nvs_flash_init_partition(kPartitionLabel);
        if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            Serial.println("[设备身份] 专用分区尚未格式化，将只初始化 identity 区域。");
            error = nvs_flash_erase_partition(kPartitionLabel);
            if (error == ESP_OK)
                error = nvs_flash_init_partition(kPartitionLabel);
        }

        if (error != ESP_OK)
        {
            Serial.printf("[设备身份] identity 分区初始化失败：%s。\n", esp_err_to_name(error));
            return false;
        }
        return true;
    }
}

bool SysDeviceIdentity_Init()
{
    s_storageReady = false;
    s_provisioned = false;
    memset(&s_record, 0, sizeof(s_record));
    s_machineCode = BuildMachineCode();

    if (s_machineCode.length() == 0)
        Serial.println("[设备身份] 无法读取 eFuse 基础 MAC，机器码暂不可用。");

    if (!PrepareIdentityPartition())
        return false;

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
    {
        s_storageReady = true;
        Serial.println("[设备身份] 专用存储已就绪，当前设备尚未绑定服务器身份。");
        return true;
    }
    if (error != ESP_OK)
    {
        Serial.printf("[设备身份] 无法打开身份命名空间：%s。\n", esp_err_to_name(error));
        return false;
    }

    size_t recordLength = sizeof(s_record);
    error = nvs_get_blob(handle, kRecordKey, &s_record, &recordLength);
    nvs_close(handle);

    if (error == ESP_ERR_NVS_NOT_FOUND)
    {
        memset(&s_record, 0, sizeof(s_record));
        s_storageReady = true;
        Serial.println("[设备身份] 专用存储已就绪，当前设备尚未绑定服务器身份。");
        return true;
    }
    if (error != ESP_OK || recordLength != sizeof(s_record) || !IsRecordValid(s_record))
    {
        memset(&s_record, 0, sizeof(s_record));
        Serial.println("[设备身份] 身份记录损坏或版本不兼容，已锁定绑定入口以保留现场。");
        return false;
    }

    s_storageReady = true;
    s_provisioned = true;
    Serial.printf("[设备身份] 已载入公开身份 %s，凭据版本 %lu。\n",
                  s_record.public_id,
                  static_cast<unsigned long>(s_record.credential_version));
    return true;
}

SysDeviceIdentitySnapshot SysDeviceIdentity_GetSnapshot()
{
    SysDeviceIdentitySnapshot snapshot;
    snapshot.storage_ready = s_storageReady;
    snapshot.provisioned = s_provisioned;
    snapshot.schema_version = kSchemaVersion;
    snapshot.credential_version = s_provisioned ? s_record.credential_version : 0;
    snapshot.machine_code = s_machineCode;
    snapshot.public_id = s_provisioned ? String(s_record.public_id) : String();
    return snapshot;
}

String SysDeviceIdentity_BuildPublicResponse()
{
    const SysDeviceIdentitySnapshot snapshot = SysDeviceIdentity_GetSnapshot();
    JsonDocument document;
    document["schema_version"] = snapshot.schema_version;
    document["storage_ready"] = snapshot.storage_ready;
    document["provisioned"] = snapshot.provisioned;
    document["credential_version"] = snapshot.credential_version;
    document["machine_code"] = snapshot.machine_code;
    document["public_id"] = snapshot.public_id;

    String response = "IDENTITY:";
    serializeJson(document, response);
    return response;
}

SysDeviceIdentityBindResult SysDeviceIdentity_Bind(const String &public_id, const String &device_key)
{
    if (!s_storageReady)
        return SysDeviceIdentityBindResult::StorageUnavailable;
    if (s_machineCode.length() == 0)
        return SysDeviceIdentityBindResult::MachineCodeUnavailable;
    if (s_provisioned)
        return SysDeviceIdentityBindResult::AlreadyBound;
    if (!IsPublicIdValid(public_id))
        return SysDeviceIdentityBindResult::InvalidPublicId;
    if (!IsDeviceKeyValid(device_key))
        return SysDeviceIdentityBindResult::InvalidDeviceKey;

    DeviceIdentityRecord pending = {};
    pending.magic = kRecordMagic;
    pending.schema_version = kSchemaVersion;
    pending.public_id_length = static_cast<uint16_t>(public_id.length());
    pending.device_key_length = static_cast<uint16_t>(device_key.length());
    pending.credential_version = 1;
    memcpy(pending.public_id, public_id.c_str(), pending.public_id_length);
    memcpy(pending.device_key, device_key.c_str(), pending.device_key_length);
    pending.public_id[pending.public_id_length] = '\0';
    pending.device_key[pending.device_key_length] = '\0';
    pending.checksum = CalculateChecksum(pending);

    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open_from_partition(kPartitionLabel, kNamespace, NVS_READWRITE, &handle);
    if (error == ESP_OK)
    {
        error = nvs_set_blob(handle, kRecordKey, &pending, sizeof(pending));
        if (error == ESP_OK)
            error = nvs_commit(handle);
        nvs_close(handle);
    }

    if (error != ESP_OK)
    {
        Serial.printf("[设备身份] 首次绑定写入失败：%s。\n", esp_err_to_name(error));
        return SysDeviceIdentityBindResult::SaveFailed;
    }

    s_record = pending;
    s_provisioned = true;
    Serial.printf("[设备身份] 已完成首次绑定，公开身份为 %s；通行码未写入日志。\n", s_record.public_id);
    return SysDeviceIdentityBindResult::Ok;
}

bool SysDeviceIdentity_CopyAuthCredentials(String &machine_code, String &device_key)
{
    machine_code = "";
    device_key = "";
    if (!s_storageReady || !s_provisioned || s_machineCode.length() == 0)
        return false;

    machine_code = s_machineCode;
    device_key = String(s_record.device_key);
    return true;
}
