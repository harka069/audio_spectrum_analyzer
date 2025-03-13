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
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
set(PICO_DEFAULT_FLOAT_IMPL pico_float_pico)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(audio_spectrum_analyzer C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(audio_spectrum_analyzer audio_spectrum_analyzer.c )

pico_set_program_name(audio_spectrum_analyzer "audio_spectrum_analyzer")
pico_set_program_version(audio_spectrum_analyzer "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(audio_spectrum_analyzer 0)
pico_enable_stdio_usb(audio_spectrum_analyzer 1)
add_subdirectory(lib/ili9341)
add_subdirectory(lib/gfx)

# Add the standard library to the build
target_link_libraries(audio_spectrum_analyzer
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(audio_spectrum_analyzer PRIVATE
${CMAKE_CURRENT_LIST_DIR}/../CMSISDSP/CMSIS-DSP/Include
${CMAKE_CURRENT_LIST_DIR}/../CMSISDSP/CMSIS_6/CMSIS/Core/Include
)

# Add any user requested libraries
target_link_libraries(audio_spectrum_analyzer 
        pico_stdlib 
        pico_rand
        hardware_spi
        hardware_dma
        hardware_timer
        hardware_adc
        ili9341
        gfx
        ${CMAKE_CURRENT_LIST_DIR}/../CMSISDSP/build/bin_dsp/libCMSISDSP.a 
        )
add_compile_definitions(PICO_DEOPTIMIZED_DEBUG=1)
pico_add_extra_outputs(audio_spectrum_analyzer)

