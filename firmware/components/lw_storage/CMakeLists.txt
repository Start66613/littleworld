idf_component_register(SRCS "src/lw_storage.c" "src/lw_sd.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES freertos heap esp32_p4_wifi6_touch_lcd_7b)
