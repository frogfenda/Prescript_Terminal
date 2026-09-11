# 【构建职责】仅供 Arduino + ESP-IDF 混合实验环境使用；普通 Arduino 环境仍由 PlatformIO 原流程构建。
cmake_minimum_required(VERSION 3.16.0)

# 【兼容说明】同一台电脑缓存多个 Arduino-ESP32 版本时，PlatformIO 可能给组件目录追加版本号。
# ESP-IDF 会直接使用目录名作为组件名，因此这里从实际组件路径识别名称，避免把本机路径或版本写死。
set(PRESCRIPT_ARDUINO_COMPONENT_NAME "arduino" CACHE INTERNAL "Arduino component name selected by PlatformIO")
set(PRESCRIPT_ARDUINO_COMPONENT_DIR "" CACHE INTERNAL "Arduino component directory selected by PlatformIO")
foreach(prescript_component_dir IN LISTS EXTRA_COMPONENT_DIRS)
    get_filename_component(prescript_component_name "${prescript_component_dir}" NAME)
    if(prescript_component_name MATCHES "^framework-arduinoespressif32")
        set(PRESCRIPT_ARDUINO_COMPONENT_NAME "${prescript_component_name}" CACHE INTERNAL
            "Arduino component name selected by PlatformIO" FORCE)
        set(PRESCRIPT_ARDUINO_COMPONENT_DIR "${prescript_component_dir}" CACHE INTERNAL
            "Arduino component directory selected by PlatformIO" FORCE)
    endif()
endforeach()

# 【USB兼容】Arduino-ESP32 2.0.x 使用旧名称判断 TinyUSB 总开关，而 ESP-IDF 4.4
# 生成的是 CONFIG_TINYUSB；混合构建时补上兼容宏，确保 CDC/MSC 类及其实现同时参与编译。
add_compile_definitions(
    CONFIG_TINYUSB_ENABLED=1
    CFG_TUD_ENDOINT0_SIZE=CFG_TUD_ENDPOINT0_SIZE
    CONFIG_LITTLEFS_PAGE_SIZE=256)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_prescript_terminal)

# 【TinyUSB依赖】PlatformIO 的混合框架适配没有把 Arduino 组件声明的 arduino_tinyusb
# 映射到 ESP-IDF 4.4 自带的 tinyusb 组件；显式建立依赖以同时获得头文件与链接实现。
idf_component_get_property(prescript_arduino_lib ${PRESCRIPT_ARDUINO_COMPONENT_NAME} COMPONENT_LIB)
idf_component_get_property(prescript_tinyusb_lib tinyusb COMPONENT_LIB)
target_link_libraries(${prescript_arduino_lib} PUBLIC ${prescript_tinyusb_lib})

# 【LittleFS兼容】Arduino-only 构建会自动链接随框架发布的 esp_littlefs 静态库；
# 混合构建不会完成这一步。这里复用同版本、同芯片的框架库，不复制源码，也不修改本机包。
set(prescript_littlefs_dir "${PRESCRIPT_ARDUINO_COMPONENT_DIR}/tools/sdk/esp32s3")
add_library(prescript_esp_littlefs STATIC IMPORTED GLOBAL)
set_target_properties(prescript_esp_littlefs PROPERTIES
    IMPORTED_LOCATION "${prescript_littlefs_dir}/lib/libesp_littlefs.a"
    INTERFACE_INCLUDE_DIRECTORIES "${prescript_littlefs_dir}/include/esp_littlefs/include")
target_link_libraries(${prescript_arduino_lib} PUBLIC prescript_esp_littlefs)
