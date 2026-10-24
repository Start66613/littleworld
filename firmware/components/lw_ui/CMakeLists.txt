idf_component_register(
    SRCS
        "src/lw_ui.c"
        "src/lw_theme.c"
        "src/lw_widgets.c"
        "src/lw_icons.c"
        "src/lw_apps.c"
        "src/lw_app_state.c"
        "src/lw_shell.c"
        "src/lw_pet_view.c"
        "src/lw_app_home.c"
        "src/lw_app_pet.c"
        "src/lw_app_album.c"
        "src/lw_app_music.c"
        "src/lw_app_memories.c"
        "src/lw_app_settings.c"
        "src/lw_arcade.c"
        "fonts/lw_font_18.c"
        "fonts/lw_font_22.c"
        "fonts/lw_font_28.c"
        "fonts/lw_font_44.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl__lvgl lw_core arcade_core
    PRIV_REQUIRES esp32_p4_wifi6_touch_lcd_7b
)