add_executable(floppy main.c memory.c buffers.c disk.c)

pico_set_binary_type(floppy copy_to_ram)

pico_generate_pio_header(floppy "${CMAKE_CURRENT_LIST_DIR}/readwrite.pio")

target_link_libraries(floppy PUBLIC pico_stdlib hardware_pwm hardware_pio pico_multicore)
target_include_directories(floppy PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)

# add_executable(tinyusb_test tinyusb_test.c)

# target_sources(tinyusb_test PUBLIC 
#   ${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk/lib/tinyusb/lib/fatfs/source/ff.c
#   ${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk/lib/tinyusb/lib/fatfs/source/ffsystem.c
#   ${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk/lib/tinyusb/lib/fatfs/source/ffunicode.c
#   )

# target_link_libraries(tinyusb_test PUBLIC pico_stdlib tinyusb_host)
# target_include_directories(tinyusb_test PUBLIC 
#   ${CMAKE_CURRENT_LIST_DIR}/ 
#   ${CMAKE_CURRENT_SOURCE_DIR}/../pico-sdk/lib/tinyusb/lib/fatfs/source/
# )


# add_executable(usb_msc usb_msc.c semihosting.c)
add_executable(usb_msc usb_msc.c)

target_link_libraries(usb_msc PUBLIC pico_stdlib pico_multicore hardware_pio hardware_dma fatfs tinyusb_host pico_pio_usb)

target_include_directories(usb_msc PUBLIC 
  ${CMAKE_CURRENT_LIST_DIR}/
  # pico-sdk/lib/tinyusb/hw/mcu/raspberry_pi/Pico-PIO-USB/src/
)

