cmake_minimum_required(VERSION 3.20)
project(stm32f4_projects C ASM)

set(PROJECT "" CACHE STRING "Project to build (subdir of projects)")
if(PROJECT STREQUAL "")
  message(FATAL_ERROR "Set -DPROJECT=<name>, ex: -DPROJECT=gpio")
endif()

# Output per-example
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# Toolchain
if(NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/arm-none-eabi.cmake" CACHE FILEPATH "" FORCE)
endif()

# ----- Sources -----
file(GLOB SRC_LIB_CLI "${CMAKE_SOURCE_DIR}/libs/cli/*.c")
if(PROJECT STREQUAL "freertos")
  file(GLOB SRC_LIB_FREERTOS "${CMAKE_SOURCE_DIR}/libs/freertos/*.c")
  file(GLOB SRC_LIB_FREERTOS_PORT "${CMAKE_SOURCE_DIR}/libs/freertos/portable/GCC/ARM_CM4F/*.c")
else()
  set(SRC_LIB_FREERTOS "")
  set(SRC_LIB_FREERTOS_PORT "")
endif()
file(GLOB SRC_HAL   ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Src/*.c)
file(GLOB SRC_LEG   ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Src/Legacy/*.c)

set(PRJ_DIR ${CMAKE_SOURCE_DIR}/examples/${PROJECT})
if(NOT EXISTS "${PRJ_DIR}/main.c")
  message(FATAL_ERROR "Not found examples/${PROJECT}/main.c")
endif()
file(GLOB SRC_PROJECT CONFIGURE_DEPENDS
    "${PRJ_DIR}/*.c"
)

set(SRC_STARTUP ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/startup/startup_stm32f411xe.s)

set(TARGET_NAME ${PROJECT})
add_executable(${TARGET_NAME}
  ${SRC_LIB_CLI}
  ${SRC_LIB_FREERTOS}
  ${SRC_LIB_FREERTOS_PORT}
  ${SRC_HAL}
  ${SRC_LEG}
  ${SRC_PROJECT}
  ${SRC_STARTUP}
)

option(USE_BOOT_APP_LD "Use split boot/app linker scripts" OFF)

set(LD_DEFAULT ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/linker/STM32F411CEUX_FLASH.ld)
set(LD_BOOT    ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/linker/boot.ld)
set(LD_APP     ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/linker/app.ld)

set(LINKER_SCRIPT ${LD_DEFAULT})

if(USE_BOOT_APP_LD)
  if(PROJECT STREQUAL "bootloader")
    set(LINKER_SCRIPT ${LD_BOOT})
    add_compile_definitions(BUILD_BL)
  else()
    set(LINKER_SCRIPT ${LD_APP})
    add_compile_definitions(BUILD_APP)
  endif()
endif()
message(STATUS "Using linker script: ${LINKER_SCRIPT}")

target_include_directories(${TARGET_NAME} PRIVATE
  ${CMAKE_SOURCE_DIR}/libs/cli
  $<$<STREQUAL:${PROJECT},freertos>:${CMAKE_SOURCE_DIR}/libs/freertos/include>
  $<$<STREQUAL:${PROJECT},freertos>:${CMAKE_SOURCE_DIR}/libs/freertos/portable/GCC/ARM_CM4F>
  ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Core
  ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device
  ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Inc
  ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Inc/Legacy
  ${PRJ_DIR}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    DEBUG
    USE_HAL_DRIVER
    STM32F411xE
)

target_link_options(${TARGET_NAME} PRIVATE
  -Wl,--gc-sections
  -Wl,-Map,$<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.map
  -T${LINKER_SCRIPT}
  -specs=nano.specs
)

target_link_libraries(${TARGET_NAME} PRIVATE
  -Wl,--start-group
  c m gcc nosys
  -Wl,--end-group
)

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.bin
  COMMENT "Generating HEX and BIN"
)
