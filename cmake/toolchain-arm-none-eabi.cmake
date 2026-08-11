# ZhiOS-AF 交叉编译工具链（arm-none-eabi）
#
# 对应《16-交叉编译与工具链配置文档》。用于 ARM Cortex-M 系列（STM32H7/MCXN947 等）。
# 使用：
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake -DZHIO_PLATFORM=stm32h743 -DZHIO_FREERTOS_DIR=<path>
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
find_program(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS "-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -O2 -ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-Wl,--gc-sections" CACHE STRING "" FORCE)
