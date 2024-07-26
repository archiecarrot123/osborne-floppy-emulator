add_executable(floppy main.c memory.c buffers.c disk.c)

pico_generate_pio_header(floppy "${CMAKE_CURRENT_LIST_DIR}/readwrite.pio")

target_link_libraries(floppy PUBLIC pico_stdlib hardware_pwm hardware_pio)
target_include_directories(floppy PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
