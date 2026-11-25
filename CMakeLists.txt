cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Needs PICO_SDK_PATH in the environment
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(stereo_tool C CXX ASM)
pico_sdk_init()

add_executable(stereo_tool main.cpp)

# ComputerCard.h is copied into ./lib by the CI workflow (or by hand)
target_include_directories(stereo_tool PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lib)

target_link_libraries(stereo_tool
    pico_stdlib pico_multicore pico_unique_id
    hardware_adc hardware_dma hardware_pwm hardware_spi hardware_i2c
    hardware_gpio hardware_irq hardware_clocks hardware_sync hardware_flash hardware_pio)

# Run from RAM: avoids XIP flash-cache jitter in the 48 kHz interrupt
pico_set_binary_type(stereo_tool copy_to_ram)

pico_enable_stdio_usb(stereo_tool 0)
pico_enable_stdio_uart(stereo_tool 0)

# Generates stereo_tool.uf2
pico_add_extra_outputs(stereo_tool)
