# CMakeLists.txt - BCR32 bare-metal demo, GCC build (GNU Arm Embedded toolchain)
#
#   cmake -S demo/Project/gcc -B build -G Ninja
#   cmake --build build
#
# Outputs in the build folder: bcr32_demo.elf, .hex, .bin, .map. With BCR32_UPDATE_IMAGE
# (default ON on Windows) the build also copies the update binary, as the Keil
# After Build: tools/update/BCR32_FW_V<version>_<date>_<time>.bin
cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/arm-none-eabi.cmake)
project(bcr32_demo C ASM)

# CMAKE_HOST_WIN32: the build machine (WIN32 describes the target, false for the MCU)
option(BCR32_UPDATE_IMAGE "Copy the update binary to tools/update after the build (Windows)" ${CMAKE_HOST_WIN32})

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(DEMO ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)

set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/startup_bcr32_gcc.S

    ${DEMO}/App/main.c
    ${DEMO}/App/input.c
    ${DEMO}/App/seq.c
    ${DEMO}/App/cv_out.c
    ${DEMO}/App/activity_led.c
    ${DEMO}/App/boot_jump.c
    ${DEMO}/App/sysex.c

    ${DEMO}/Board/bsp_gpio.c
    ${DEMO}/Board/bsp_delay.c
    ${DEMO}/Board/bsp_led.c
    ${DEMO}/Board/bsp_buttons.c
    ${DEMO}/Board/bsp_cv.c
    ${DEMO}/Board/bsp_midi_uart.c
    ${DEMO}/Board/bsp_spi1.c
    ${DEMO}/Board/bsp_flash.c
    ${DEMO}/Board/bsp_iflash.c
    ${DEMO}/Board/bsp_usb.c

    ${DEMO}/Drivers/GD32F4xx/system_gd32f4xx.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_rcu.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_gpio.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_misc.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_i2c.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_spi.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_pmu.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_dma.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_exti.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_syscfg.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_usart.c
    ${DEMO}/Drivers/GD32F4xx/gd32f4xx_fmc.c

    ${DEMO}/UsbDriver/core/Source/usbd_core.c
    ${DEMO}/UsbDriver/core/Source/usbd_enum.c
    ${DEMO}/UsbDriver/core/Source/usbd_transc.c
    ${DEMO}/UsbDriver/driver/Source/drv_usb_core.c
    ${DEMO}/UsbDriver/driver/Source/drv_usb_dev.c
    ${DEMO}/UsbDriver/driver/Source/drv_usbd_int.c
    ${DEMO}/UsbDriver/midi/src/usbd_desc.c
    ${DEMO}/UsbDriver/midi/src/usbd_midi.c

    ${DEMO}/ThirdParty/ssd1306/ssd1306.c
    ${DEMO}/ThirdParty/ssd1306/ssd1306_fonts.c
)

add_executable(bcr32_demo ${SOURCES})
set_target_properties(bcr32_demo PROPERTIES SUFFIX ".elf")

target_include_directories(bcr32_demo PRIVATE
    ${DEMO}/App
    ${DEMO}/Board
    ${DEMO}/Drivers/CMSIS
    ${DEMO}/Drivers/GD32F4xx
    ${DEMO}/ThirdParty/ssd1306
    ${DEMO}/UsbDriver/core/Include
    ${DEMO}/UsbDriver/driver/Include
    ${DEMO}/UsbDriver/midi/inc
)

target_compile_definitions(bcr32_demo PRIVATE GD32F405 USE_STDPERIPH_DRIVER USE_USB_FS)

set(CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)

target_compile_options(bcr32_demo PRIVATE
    ${CPU_FLAGS}
    $<$<COMPILE_LANGUAGE:C>:-std=gnu99>
    -O2 -g
    -ffunction-sections -fdata-sections
    -Wall
)

target_link_options(bcr32_demo PRIVATE
    ${CPU_FLAGS}
    -T${CMAKE_CURRENT_LIST_DIR}/bcr32_demo.ld
    --specs=nano.specs --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/bcr32_demo.map
    -Wl,--print-memory-usage
)
set_target_properties(bcr32_demo PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/bcr32_demo.ld)

add_custom_command(TARGET bcr32_demo POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:bcr32_demo> ${CMAKE_CURRENT_BINARY_DIR}/bcr32_demo.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bcr32_demo> ${CMAKE_CURRENT_BINARY_DIR}/bcr32_demo.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:bcr32_demo>
    VERBATIM
)

if(BCR32_UPDATE_IMAGE)
    # the same step as the Keil After Build: bcr32_demo.bin -> generate_BCR.bat (copy to tools/update)
    file(TO_NATIVE_PATH ${DEMO}/tools/update/generate_BCR.bat UPD_BAT)
    file(TO_NATIVE_PATH ${CMAKE_CURRENT_BINARY_DIR}/bcr32_demo.bin UPD_BIN)
    add_custom_command(TARGET bcr32_demo POST_BUILD
        COMMAND cmd /c ${UPD_BAT} ${UPD_BIN}
        VERBATIM
    )
endif()
