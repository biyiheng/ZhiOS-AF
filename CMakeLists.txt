# ZhiOS-AF 顶层构建（CMake）
#
# 对应《16-交叉编译与工具链配置文档》《15-Docker容器化构建与部署文档》。
# 通过 -DZHIO_PLATFORM=<name> 选择目标平台；默认 host（可在主机/容器内运行测试与演示）。
#   host         : 主机仿真（pthread 端口 + 主机 HAL/BSP），用于 ZTEST 单元测试与演示
#   stm32h743    : STM32H743 (Cortex-M7)
#   stm32h755    : STM32H755 (Cortex-M7+M4)
#   mcxn947      : NXP MCXN947 (Cortex-M33/eIQ Neutron)
#   cortex_m55   : Arm Cortex-M55 (Ethos-U55)
#   esp32s3      : ESP32-S3
#   rp2350       : Raspberry Pi RP2350
cmake_minimum_required(VERSION 3.16)
project(zhi_os_af C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED ZHIO_PLATFORM)
    set(ZHIO_PLATFORM "host")
endif()
message(STATUS "ZhiOS-AF platform: ${ZHIO_PLATFORM}")

# 头文件
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------------- 核心库（与平台无关的 ZhiOS 核心） ----------------
set(ZHIO_CORE_SOURCES
    kernel/kernel.c
    ai_kernel/tensor_mem/tensor_mem.c
    ai_kernel/npu_dsp/npu_dsp.c
    ai_kernel/inference_scheduler/inference_scheduler.c
    ai_kernel/security/security.c
    ai_kernel/security/firewall.c
    model_runtime/model_runtime.c
    capability/capability.c
    agent/message_bus/message_bus.c
    agent/auto_agent/agent.c
    agent/sub_agents/sub_agents.c
    ai_service/ai_service.c
    ai_service/cloud/cloud_transport.c
    ai_service/cloud/adapters/openai/openai_adapter.c
    ai_service/cloud/adapters/claude/claude_adapter.c
    ai_service/cloud/adapters/a2a/a2a_adapter.c
    ai_service/cloud/adapters/mcp/mcp_adapter.c
    comm/comm.c
    tools/ztest/ztest.c
)
add_library(zhio_core STATIC ${ZHIO_CORE_SOURCES})

# ---------------- 平台相关（RTOS 端口 + HAL + BSP，可替换模块） ----------------
if(ZHIO_PLATFORM STREQUAL "host")
    set(ZHIO_PLATFORM_SOURCES
        rtos/host/zhio_rtos_port.c
        hal/hal_host.c
        bsp/host/board.c
    )
    find_package(Threads REQUIRED)
    set(ZHIO_PLATFORM_LIBS Threads::Threads)
else()
    # 真实 MCU：使用 FreeRTOS 端口 + 对应 BSP（可替换模块）。
    # 通过 -DZHIO_PLATFORM=<name> 选择开发板，FreeRTOS 源码通过 ZHIO_FREERTOS_DIR 提供。
    if(NOT DEFINED ZHIO_FREERTOS_DIR)
        message(FATAL_ERROR "For MCU platforms set -DZHIO_FREERTOS_DIR=<path> to FreeRTOS kernel source")
    endif()
    set(ZHIO_PLATFORM_SOURCES rtos/freertos/zhio_rtos_port.c)

    # 各 BSP 提供 hal.h 实现与 board_init；不同开发板在此替换（可替换模块设计）。
    if(ZHIO_PLATFORM STREQUAL "stm32h743")
        list(APPEND ZHIO_PLATFORM_SOURCES
            bsp/stm32h743/board.c
            bsp/stm32h743/hal_stm32h743.c)
    elseif(ZHIO_PLATFORM STREQUAL "stm32h755")
        message(FATAL_ERROR "stm32h755 需要新增 bsp/stm32h755（board.c + hal_stm32h755.c），可参考 bsp/stm32h743")
    elseif(ZHIO_PLATFORM STREQUAL "mcxn947")
        message(FATAL_ERROR "mcxn947 需要新增 bsp/mcxn947（board.c + hal_mcxn947.c），可参考 bsp/stm32h743")
    else()
        message(FATAL_ERROR "Unsupported MCU platform: ${ZHIO_PLATFORM}")
    endif()

    include_directories(${ZHIO_FREERTOS_DIR}/include)
    add_definitions(-DZHIO_USE_FREERTOS)
    set(ZHIO_PLATFORM_LIBS "")
endif()
add_library(zhio_platform STATIC ${ZHIO_PLATFORM_SOURCES})

# ---------------- 可执行目标 ----------------
# 单元测试（ZTEST）
add_executable(zhio_tests tests/test_main.c tests/test_core.c tests/test_agent.c)
target_link_libraries(zhio_tests PRIVATE zhio_core zhio_platform ${ZHIO_PLATFORM_LIBS})

# 演示程序
add_executable(zhio_demo examples/demo_main.c)
target_link_libraries(zhio_demo PRIVATE zhio_core zhio_platform ${ZHIO_PLATFORM_LIBS})

# 可选的 host 示例（5 个示例应用共享 demo 结构，当前以 demo 为代表）
enable_testing()
add_test(NAME zhio_tests COMMAND zhio_tests)
