#include "sys_ble_queue.h"
#include "sys_constants.h"
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
