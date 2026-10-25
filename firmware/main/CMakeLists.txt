idf_component_register(
    SRCS "app_main.c" "lw_boot.c"
    INCLUDE_DIRS "."
    # lvgl__lvgl is already required by lw_ui, but lw_boot.c includes "lvgl.h"
    # directly for the capability timer, so the dependency is declared here too.
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl lw_ui lw_system lw_backend lw_net nvs_flash esp_timer
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
