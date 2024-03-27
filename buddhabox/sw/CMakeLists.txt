cmake_minimum_required(VERSION 3.25)

# Pull in SDK (must be before project)
include(/opt/pico/pico-sdk/external/pico_sdk_import.cmake)

project(bb_blinky C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()


add_executable(buddhabox
	buddhabox.c
	debounce.c
)

pico_enable_stdio_uart(buddhabox 1)

target_link_libraries(buddhabox
	pico_stdlib              # for core functionality
	hardware_adc
	hardware_dma
	hardware_pio
	hardware_timer
	hardware_clocks
	hardware_pwm
	pico_unique_id
)

# create map/bin/hex file etc.
pico_add_extra_outputs(buddhabox)
