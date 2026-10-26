idf_component_register(
    SRCS "src/lw_store.c" "src/lw_store_nvs.c" "src/lw_hw.c" "src/lw_diag.c" "src/lw_diag_report.c" "src/lw_recovery.c"
    INCLUDE_DIRS "include"
    REQUIRES lw_core
    PRIV_REQUIRES esp32_p4_wifi6_touch_lcd_7b nvs_flash esp_timer
)

