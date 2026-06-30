/*
【模块职责】BLE 接收队列实现。用临界区保护固定上限队列，满队列时丢弃最旧消息，避免网页连发命令占满内存。
【阅读提示】本文件注释按“对外接口说明在 .h、内部实现步骤在 .cpp”的原则补充；注释描述当前代码实际行为，不把未实现功能写成已实现。
*/
#include "sys/sys_ble_queue.h"
#include "sys/sys_constants.h"
#include <queue>
#include <mutex>

static std::queue<String> s_ble_msg_queue;
static std::mutex s_ble_mutex;

void SysBleQueue_Push(const String& msg)
{
    std::lock_guard<std::mutex> lock(s_ble_mutex);
    while (s_ble_msg_queue.size() >= PrescriptConst::MAX_BLE_QUEUE)
    {
        s_ble_msg_queue.pop(); // Drop oldest payload to avoid unbounded heap growth.
    }
    s_ble_msg_queue.push(msg);
}

bool SysBleQueue_Pop(String& out)
{
    std::lock_guard<std::mutex> lock(s_ble_mutex);
    if (s_ble_msg_queue.empty()) return false;
    out = s_ble_msg_queue.front();
    s_ble_msg_queue.pop();
    return true;
}

size_t SysBleQueue_Size()
{
    std::lock_guard<std::mutex> lock(s_ble_mutex);
    return s_ble_msg_queue.size();
}
