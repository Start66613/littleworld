cmake_minimum_required(VERSION 3.16)
project(little_world_host C)

set(CMAKE_C_STANDARD 11)

set(LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../firmware/managed_components/lvgl__lvgl" CACHE PATH "Pinned LVGL 9.5.0 source")
if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
    message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}; build the firmware once so the component manager fetches it")
endif()
file(READ "${LVGL_DIR}/lv_version.h" LVGL_VERSION_HEADER)
if(NOT LVGL_VERSION_HEADER MATCHES "LVGL_VERSION_MAJOR 9" OR
   NOT LVGL_VERSION_HEADER MATCHES "LVGL_VERSION_MINOR 5" OR
   NOT LVGL_VERSION_HEADER MATCHES "LVGL_VERSION_PATCH 0")
    message(FATAL_ERROR "Little World requires pinned LVGL 9.5.0")
endif()

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_LVGL_H_SIMPLE_INCLUDE ON CACHE BOOL "" FORCE)
add_subdirectory("${LVGL_DIR}" lvgl_build EXCLUDE_FROM_ALL)

file(GLOB LW_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src/*.c")
file(GLOB LW_UI_ARCADE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src/arcade/*.c")
file(GLOB LW_UI_FONTS "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/fonts/*.c")
file(GLOB LW_CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_core/src/*.c")
file(GLOB ARCADE_CORE_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/arcade_core/src/*.c")
file(GLOB LW_DUO_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_duo/src/*.c")
set(LW_RPG_SOURCES ../rpg/src/lw_rpg.c ../rpg/src/lw_rpg_save.c ../rpg/data/lw_rpg_home.c)

add_executable(LittleWorld
    main_host.c
    auto_host.c
    ui_v2_test.c
    pixel_pack.c
    pixel_duo_host.c
    os_host.c
    ../firmware/components/lw_storage/src/lw_storage.c
    ../firmware/components/lw_system/src/lw_system.c
    win_display.c
    host_platform.c
    ${LW_UI_SOURCES}
    ${LW_UI_ARCADE_SOURCES}
    ${LW_UI_FONTS}
    ${LW_CORE_SOURCES}
    ${LW_RPG_SOURCES}
    ${ARCADE_CORE_SOURCES}
    ${LW_DUO_SOURCES})

target_include_directories(LittleWorld PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_ui/src"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_core/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/arcade_core/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_duo/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_system/include"
    "${CMAKE_CURRENT_LIST_DIR}/../firmware/components/lw_storage/include"
    "${CMAKE_CURRENT_LIST_DIR}/../rpg/include"
    "${CMAKE_CURRENT_LIST_DIR}/../rpg/data"
    "${LVGL_DIR}")

target_compile_definitions(LittleWorld PRIVATE LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE)

if(MSVC)
    target_compile_options(LittleWorld PRIVATE /utf-8)
endif()
add_executable(StorageTests ../tests/host/test_storage.c ../firmware/components/lw_storage/src/lw_storage.c)
target_include_directories(StorageTests PRIVATE ../firmware/components/lw_storage/include)
if(MSVC)
    target_compile_options(StorageTests PRIVATE /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()
add_executable(RPGModelTests ../tests/host/test_rpg.c ${LW_RPG_SOURCES})
target_include_directories(RPGModelTests PRIVATE ../rpg/include ../rpg/data)
if(MSVC)
    target_compile_options(RPGModelTests PRIVATE /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()

target_link_libraries(LittleWorld PRIVATE lvgl user32 gdi32 psapi)

add_executable(PixelDuoModelTests ../tests/host/test_lw_duo.c ${LW_DUO_SOURCES})
target_include_directories(PixelDuoModelTests PRIVATE ../firmware/components/lw_duo/include)
if(MSVC)
    target_compile_options(PixelDuoModelTests PRIVATE /W4 /WX /utf-8)
endif()
add_executable(SystemModelTests ../tests/host/test_lw_system.c ../firmware/components/lw_system/src/lw_system.c ${LW_CORE_SOURCES})
target_include_directories(SystemModelTests PRIVATE ../firmware/components/lw_system/include ../firmware/components/lw_core/include)
if(MSVC)
    target_compile_options(SystemModelTests PRIVATE /W4 /WX /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()
