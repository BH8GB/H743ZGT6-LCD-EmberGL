cmake_minimum_required(VERSION 3.22)

#
# Project file managed by hand (CubeMX overwrites the generated parts under
# cmake/stm32cubemx/ but keeps this file's content).
# Keep comments ASCII: this file is rewritten by CubeMX after each generation
# and non-ASCII text got mangled before.
#

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME H743ZGT6_DDR3_LCD)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

# EmberGL is C++; enable the language only when the port is turned on.
set(LCD_ENABLE_EMBERGL 1 CACHE BOOL "Build EmberGL port and demo")
if(LCD_ENABLE_EMBERGL)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_executable(${CMAKE_PROJECT_NAME})

# STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
)

# ---- LCD driver (BSP) ----
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    BSP/Src/lcd.c
    BSP/Src/lcd_bus.c
    BSP/Src/lcd_ili9806.c
    BSP/Src/lcd_raster.c
    BSP/Src/gt911.c
    BSP/Src/lcd_canvas.c
    BSP/Src/lcd_dma.c
    BSP/Src/lcd_selftest.c
    BSP/Port/lcd_port_lvgl.c          # empty unless LCD_PORT_LVGL=1
)

if(LCD_ENABLE_EMBERGL)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE LCD_PORT_EMBERGL=1)
    # EmberGL headers expand templates in our own .cpp too; silence their
    # upstream diagnostics at target level.
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-switch;-Wno-int-in-bool-context;-Wno-unused-parameter>
    )
    set(EMBERGL_DIR "${CMAKE_SOURCE_DIR}/third_party/EmberGL")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        BSP/Port/lcd_port_embergl.cpp
        BSP/Port/lcd_port_embergl_demo.cpp
        ${EMBERGL_DIR}/src/egl_core.cpp
        ${EMBERGL_DIR}/src/egl_mesh.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer_cache.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer_tiling.cpp
        ${EMBERGL_DIR}/src/egl_texture.cpp
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${EMBERGL_DIR}/src
        ${EMBERGL_DIR}/assets
    )

    # Upstream EmberGL emits a few -Wall diagnostics of its own; keep our
    # build at 0 warnings without patching vendored sources.
    set_source_files_properties(
        ${EMBERGL_DIR}/src/egl_core.cpp
        ${EMBERGL_DIR}/src/egl_mesh.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer_cache.cpp
        ${EMBERGL_DIR}/src/egl_rasterizer_tiling.cpp
        ${EMBERGL_DIR}/src/egl_texture.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-int-in-bool-context;-Wno-unused-parameter;-Wno-switch"
    )
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    BSP/Inc
    BSP/Port
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

# Big buffers (canvas / LVGL) live in .bss (DTCM) by default: no flash cost and
# no linker option needed. For a DMA-capable buffer see BSP/README.md (AXI SRAM).

# ---- generate .hex / .bin (CubeMX only produces .elf) ----
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex / .bin"
)
