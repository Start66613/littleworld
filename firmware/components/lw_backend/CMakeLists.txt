# The speaker test needs something to play. The track is a 22.05 kHz mono WAV
# inside the app image rather than on the card: the SD slot is mounted later
# than the codec and a first sound should not depend on removable storage that
# the user may not have inserted.
idf_component_register(
    SRCS "src/lw_store.c" "src/lw_store_nvs.c" "src/lw_hw.c" "src/lw_bgm.c" "src/lw_diag.c" "src/lw_diag_report.c" "src/lw_recovery.c"
    INCLUDE_DIRS "include"
    REQUIRES lw_core
    PRIV_REQUIRES esp32_p4_wifi6_touch_lcd_7b nvs_flash esp_timer
    EMBED_FILES "bgm/cozy.wav"
)
