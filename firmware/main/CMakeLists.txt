idf_component_register(
    SRCS "app_main.c" "lw_boot.c" "lw_art.c" "lw_capture.c" "lw_diag_board.c" "lw_content_boot.c"
    # Character art is embedded in the application image (no TF card).
    EMBED_FILES "art/characters.lwfa"
    INCLUDE_DIRS "."
    # lvgl__lvgl is already required by lw_ui, but lw_boot.c includes "lvgl.h"
    # directly for the capability timer, so the dependency is declared here too.
    # lw_storage is required for the TF-card resource set (lw_resource_mount).
    # esp_app_format provides esp_app_desc.h for lw_diag_board.c.
    # lw_content is the V4.2 content container read at boot (lw_content_boot.c).
    REQUIRES esp32_p4_wifi6_touch_lcd_7b lvgl__lvgl lw_ui lw_system lw_backend lw_net lw_storage lw_content nvs_flash esp_timer console esp_app_format
)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
