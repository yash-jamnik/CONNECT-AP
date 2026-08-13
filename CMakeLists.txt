cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(NRF52840_NORMAL_CONNECTION_VER_2)

target_include_directories(app PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/driver/ots
	${CMAKE_CURRENT_SOURCE_DIR}/driver/uart_cmd
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(app PRIVATE src/main.c)

add_subdirectory(driver/ots)
add_subdirectory(driver/uart_cmd)

