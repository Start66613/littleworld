cmake_minimum_required(VERSION 3.16)
project(little_world_host C)

set(CMAKE_C_STANDARD 11)

set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../firmware/managed_components/lvgl__lvgl")
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}; build the firmware once so the component manager fetches it")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_LVGL_H_SIMPLE_INCLUDE ON CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl_build EXCLUDE_FROM_ALL)

file(GLOB LW_UI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src/*.c")
file(GLOB LW_UI_ARCADE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src/arcade/*.c")
file(GLOB LW_UI_FONTS "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/fonts/*.c")
file(GLOB LW_CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_core/src/*.c")
file(GLOB ARCADE_CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/arcade_core/src/*.c")

add_executable(LittleWorld
    main_host.c
    auto_host.c
    win_display.c
    host_platform.c
    ${LW_UI_SOURCES}
    ${LW_UI_ARCADE_SOURCES}
    ${LW_UI_FONTS}
    ${LW_CORE_SOURCES}
    ${ARCADE_CORE_SOURCES})

target_include_directories(LittleWorld PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_core/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/arcade_core/include"
    "${LVGL_DIR}")

target_compile_definitions(LittleWorld PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

if(MSVC)
    target_compile_options(LittleWorld PRIVATE /utf-8)
endif()

target_link_libraries(LittleWorld PRIVATE lvgl user32 gdi32)
