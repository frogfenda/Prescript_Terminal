# 【构建职责】把既有 src 分层源码作为一个 ESP-IDF 应用组件编译，不改变 BSP/HAL/SYS/APP/UI 所有权。
file(GLOB_RECURSE prescript_sources
     "${CMAKE_CURRENT_LIST_DIR}/*.c"
     "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
     "${CMAKE_CURRENT_LIST_DIR}/*.S")

idf_component_register(
    SRCS ${prescript_sources}
    INCLUDE_DIRS
        "bsp/includes"
        "hal/includes"
        "sys/includes"
        "net/includes"
        "apps/includes"
        "ui/includes"
        "lang/includes"
    REQUIRES
        ${PRESCRIPT_ARDUINO_COMPONENT_NAME}
        bt)

# 【混合构建兼容】ESP-IDF 默认把全部警告升级为错误；实验环境继续输出既有源码警告，
# 但不让它们阻断 TLS 内存策略验证。正式 Arduino 环境的警告策略不受影响。
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=misleading-indentation
    -Wno-error=format-overflow
    -Wno-error=class-memaccess)
