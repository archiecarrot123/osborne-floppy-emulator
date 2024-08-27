add_executable(floppy main.c memory.c buffers.c disk.c core1.c)

pico_set_binary_type(floppy copy_to_ram)

pico_generate_pio_header(floppy "${CMAKE_CURRENT_LIST_DIR}/readwrite.pio")

target_link_libraries(floppy PUBLIC pico_stdlib hardware_pwm hardware_pio pico_multicore)
target_include_directories(floppy PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
