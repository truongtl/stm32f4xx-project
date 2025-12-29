cmake_minimum_required(VERSION 3.20)
project(stm32f4_projects C ASM)

set(PROJECT "" CACHE STRING "Project to build (subdir of examples)")
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

# ----- Auto-discover all libs -----
message(STATUS "=== Auto-discovering libraries ===")
file(GLOB LIB_DIRS LIST_DIRECTORIES true "${CMAKE_SOURCE_DIR}/libs/*")
set(SRC_ALL_LIBS "")
set(INC_ALL_LIBS "")

foreach(LIB_DIR ${LIB_DIRS})
    if(IS_DIRECTORY ${LIB_DIR})
        get_filename_component(LIB_NAME ${LIB_DIR} NAME)

        if(LIB_NAME STREQUAL "freertos")
            string(TOLOWER "${PROJECT}" PROJECT_LOWER)
            if(PROJECT_LOWER MATCHES "freertos")
                message(STATUS "  [+] freertos")

                # FreeRTOS core
                file(GLOB FREERTOS_CORE "${LIB_DIR}/*.c")
                list(APPEND SRC_ALL_LIBS ${FREERTOS_CORE})

                # FreeRTOS port
                file(GLOB FREERTOS_PORT "${LIB_DIR}/portable/GCC/ARM_CM4F/*.c")
                list(APPEND SRC_ALL_LIBS ${FREERTOS_PORT})

                # Heap implementation
                set(configHEAP_IMPLEMENTATION 4 CACHE STRING "FreeRTOS heap implementation (1-5)")
                if(configHEAP_IMPLEMENTATION GREATER 0 AND configHEAP_IMPLEMENTATION LESS 6)
                    set(FREERTOS_HEAP "${LIB_DIR}/portable/MemMang/heap_${configHEAP_IMPLEMENTATION}.c")
                    list(APPEND SRC_ALL_LIBS ${FREERTOS_HEAP})
                    message(STATUS "      Using heap_${configHEAP_IMPLEMENTATION}.c")
                else()
                    message(FATAL_ERROR "Invalid configHEAP_IMPLEMENTATION (must be 1..5)")
                endif()

                # FreeRTOS includes
                list(APPEND INC_ALL_LIBS
                    ${LIB_DIR}/include
                    ${LIB_DIR}/portable/GCC/ARM_CM4F
                )
            endif()
        else()
            list(APPEND INC_ALL_LIBS ${LIB_DIR})
            file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "${LIB_DIR}/*.c")
            if(LIB_SOURCES)
                list(APPEND SRC_ALL_LIBS ${LIB_SOURCES})
                list(LENGTH LIB_SOURCES NUM_FILES)
                message(STATUS "  [+] ${LIB_NAME}")
            endif()
        endif()
    endif()
endforeach()

# ----- HAL Drivers -----
file(GLOB SRC_HAL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Src/*.c)
file(GLOB SRC_LEG CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Src/Legacy/*.c)

# ----- Project sources -----
set(PRJ_DIR ${CMAKE_SOURCE_DIR}/examples/${PROJECT})
if(NOT EXISTS "${PRJ_DIR}/main.c")
    message(FATAL_ERROR "Not found examples/${PROJECT}/main.c")
endif()
file(GLOB SRC_PROJECT CONFIGURE_DEPENDS "${PRJ_DIR}/*.c")

# ----- Startup -----
set(SRC_STARTUP ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/startup_stm32f411xe.s)

# ----- Create executable -----
set(TARGET_NAME ${PROJECT})
add_executable(${TARGET_NAME}
    ${SRC_ALL_LIBS}
    ${SRC_HAL}
    ${SRC_LEG}
    ${SRC_PROJECT}
    ${SRC_STARTUP}
)

# ----- Linker script selection -----
# Priority:
# 1. Project-specific linker script in examples/<project>/*.ld
# 2. Default linker script in drivers/CMSIS/Device/
set(LD_DEFAULT ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device/STM32F411CEUX_FLASH.ld)

# Search for .ld file in project directory
file(GLOB PROJECT_LINKER_SCRIPTS "${PRJ_DIR}/*.ld")

if(PROJECT_LINKER_SCRIPTS)
    # Use the first .ld file found in project directory
    list(GET PROJECT_LINKER_SCRIPTS 0 LINKER_SCRIPT)
    message(STATUS "Using project-specific linker script: ${LINKER_SCRIPT}")
else()
    # Fallback to default linker script
    set(LINKER_SCRIPT ${LD_DEFAULT})
    message(STATUS "Using default linker script: ${LINKER_SCRIPT}")
endif()

# Verify linker script exists
if(NOT EXISTS ${LINKER_SCRIPT})
    message(FATAL_ERROR "Linker script not found: ${LINKER_SCRIPT}")
endif()

# ----- Include directories -----
target_include_directories(${TARGET_NAME} PRIVATE
    ${INC_ALL_LIBS}
    ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Core
    ${CMAKE_SOURCE_DIR}/drivers/CMSIS/Device
    ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${PRJ_DIR}
)

# ----- Compile definitions -----
target_compile_definitions(${TARGET_NAME} PRIVATE
    DEBUG
    USE_HAL_DRIVER
    STM32F411xE
    $<$<BOOL:${configHEAP_IMPLEMENTATION}>:configHEAP_IMPLEMENTATION=${configHEAP_IMPLEMENTATION}>
)

# ----- Linker options -----
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

# ----- Post-build: Generate HEX and BIN -----
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary -S $<TARGET_FILE:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.bin
    COMMENT "Generating HEX and BIN"
)

# ----- Post-build: Show memory usage -----
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/scripts/memory_report.py")
    # Use Python script for detailed report (cross-platform)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/memory_report.py
                ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Analyzing memory usage"
    )
else()
    # Fallback: simple size report
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "=== Memory Usage (Berkeley format) ==="
        COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${TARGET_NAME}>
        COMMAND ${CMAKE_COMMAND} -E echo ""
        # COMMAND ${CMAKE_COMMAND} -E echo "=== Memory Usage (SysV format) ==="
        # COMMAND ${CMAKE_SIZE} --format=sysv $<TARGET_FILE:${TARGET_NAME}>
        COMMENT "Analyzing memory usage"
    )
endif()