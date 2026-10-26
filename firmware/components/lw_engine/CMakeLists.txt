# Pure-C engine maths: nine-slice geometry, scene projection, one clock.
#
# No LVGL, no IDF, no allocation - so the whole component also compiles on the
# build machine and every rule in it is unit tested there.
idf_component_register(
    SRCS
        "src/lw_ui9.c"
        "src/lw_ui_asset.c"
        "src/lw_stage.c"
        "src/lw_day.c"
    INCLUDE_DIRS "include"
    REQUIRES lw_core
)
