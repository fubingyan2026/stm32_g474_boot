cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME stm32_g474_boot)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# 批量收集源代码目录
aux_source_directory(${CMAKE_SOURCE_DIR}/tasks TASKS)
aux_source_directory(${CMAKE_SOURCE_DIR}/device_drivers DEV_DRV)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/utils MID_utils)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/framework MID_FRAME_WK)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/log MID_LOG)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/key_base KEY_BASE)
aux_source_directory(${CMAKE_SOURCE_DIR}/Middlewares/protocol_tools MID_PRO)

aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/algorithm MID_algorithm)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/algorithm/pll MID_pll)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/algorithm/controller MID_controller)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/algorithm/filter MID_filter)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/algorithm/math MID_math)

# Third_Party 模块
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/Third_Party/SEGGER_RTT/RTT MID_SEGGER_RTT)
aux_source_directory(${CMAKE_SOURCE_DIR}/m_middlewares/Third_Party/ring_storage MID_RING_STORAGE)

aux_source_directory(${CMAKE_SOURCE_DIR}/service SERVICE)
aux_source_directory(${CMAKE_SOURCE_DIR}/service/boot SRV_BOOT)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${DEV_DRV}
    ${SERVICE}

    ${TASKS}
    ${MID_utils}
    ${MID_FRAME_WK}
    ${MID_LOG}
    ${KEY_BASE}
    ${MID_PRO}

    ${MID_algorithm}
    ${MID_pll}
    ${MID_controller}
    ${MID_filter}
    ${MID_math}

    ${MID_SEGGER_RTT}
    ${MID_RING_STORAGE}

    ${SRV_BOOT}

)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    tasks
    tasks/boot_task

    service
    service/boot

    device_drivers

    m_middlewares
    m_middlewares/log
    m_middlewares/protocol_tools
    m_middlewares/utils
    m_middlewares/algorithm
    m_middlewares/algorithm/pll
    m_middlewares/algorithm/controller
    m_middlewares/algorithm/filter
    m_middlewares/algorithm/math
    m_middlewares/framework

    m_middlewares/Third_Party/SEGGER_RTT/RTT/config
    m_middlewares/Third_Party/SEGGER_RTT/RTT
    m_middlewares/Third_Party/ring_storage
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# 生成Bin文件
add_custom_command(
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} ${CMAKE_PROJECT_NAME}.elf -Oihex ${CMAKE_PROJECT_NAME}.hex
        COMMAND ${CMAKE_OBJCOPY} ${CMAKE_PROJECT_NAME}.elf -Obinary ${CMAKE_PROJECT_NAME}.bin
        COMMENT "Generating hex || bin file for STM32"
)