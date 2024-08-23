cmake_minimum_required(VERSION 3.13)

# don't have a crystal
set(SKIP_HARDWARE_CLOCKS 1)
# don't use usb or power ADC
set(SKIP_PICO_RUNTIME 1)

# optimize?
# O3 perhaps a bad idea...
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# for the w25q16jv flash chip
set(PICO_DEFAULT_BOOT_STAGE2 boot2_w25q080)

# we're not using a pico
set(PICO_BOARD ofe)
set(PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")

# initialize the SDK directly
include(pico-sdk/pico_sdk_init.cmake)

project(osborne-floppy-emulator C ASM CXX)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# custom
add_subdirectory(hardware_clocks)
add_subdirectory(pico_runtime)
add_subdirectory(fatfs-cmake)

add_executable(test test.c)
target_link_libraries(test PUBLIC pico_stdlib)
target_include_directories(test PUBLIC ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(src)
