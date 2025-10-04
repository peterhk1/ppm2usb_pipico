cmake_minimum_required(VERSION 3.13...3.27)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(picotool_DIR "$ENV{HOME}/pico/picotool/")

include(pico_sdk_import.cmake)

project(ppm2usb_pipico C CXX ASM)

pico_sdk_init()

add_compile_options(-Wall
        -Wno-format
        -Wno-unused-function
        -Wno-maybe-uninitialized
        )

add_executable(${PROJECT_NAME} main.c)

target_sources(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.c
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
        )

target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(${PROJECT_NAME} pico_stdlib tinyusb_device tinyusb_board hardware_clocks pico_bootrom)

# pico_set_binary_type(${PROJECT_NAME} no_flash)

pico_add_extra_outputs(${PROJECT_NAME})

