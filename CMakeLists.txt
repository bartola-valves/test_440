# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(test_440 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

# Heavy 440tone audio engine source files
set(HEAVY_440_SOURCES
    440tone_c/Heavy_440tone.cpp
    440tone_c/HeavyContext.cpp
    440tone_c/HvHeavy.cpp
    440tone_c/HvLightPipe.c
    440tone_c/HvMessage.c
    440tone_c/HvMessagePool.c
    440tone_c/HvMessageQueue.c
    440tone_c/HvSignalPhasor.c
    440tone_c/HvSignalVar.c
    440tone_c/HvTable.c
    440tone_c/HvUtils.c
)

# DAC library sources
set(DAC_SOURCES
    lib/dac/MCP4725.cpp
)

add_executable(test_440 
    test_440.cpp
    ${HEAVY_440_SOURCES}
    ${DAC_SOURCES}
)

pico_set_program_name(test_440 "test_440")
pico_set_program_version(test_440 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(test_440 1)
pico_enable_stdio_usb(test_440 0)

# Define HV_BARE_METAL for Heavy on embedded platform
target_compile_definitions(test_440 PRIVATE
    HV_BARE_METAL=1
)

# Add the standard library to the build
target_link_libraries(test_440
        pico_stdlib
        hardware_i2c
        hardware_timer
        hardware_irq
)

# Add the standard include files to the build
target_include_directories(test_440 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/440tone_c
        ${CMAKE_CURRENT_LIST_DIR}/lib
)

pico_add_extra_outputs(test_440)

