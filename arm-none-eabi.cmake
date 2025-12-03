set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Toolchain path
set(TOOLCHAIN_BIN "C:/ST/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin")

# Compilers
set(TOOLCHAIN_PATH     ${TOOLCHAIN_BIN}/arm-none-eabi)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PATH}-gcc.exe)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PATH}-gcc.exe)
set(CMAKE_AR           ${TOOLCHAIN_PATH}-ar.exe)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PATH}-objcopy.exe)
set(CMAKE_OBJDUMP      ${TOOLCHAIN_PATH}-objdump.exe)
set(CMAKE_SIZE         ${TOOLCHAIN_PATH}-size.exe)

# CPU flags (STM32F411 = Cortex-M4F)
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

# Compile flags
set(CMAKE_C_FLAGS_INIT "${CPU_FLAGS} -std=gnu11 -g3 -fno-common -O0 -Wall -ffunction-sections -fdata-sections -fstack-usage -fcyclomatic-complexity")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

# Linker flags
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS} -Wl,--gc-sections")

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
