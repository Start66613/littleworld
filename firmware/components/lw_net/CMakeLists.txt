# Little World connectivity services: Wi-Fi state machine, time service and
# audio pipeline core. The models are plain C so they run on the host tests;
# the adapters in lw_net_esp.c are the only platform-specific part.
idf_component_register(
    SRCS
        "src/lw_wifi_model.c"
        "src/lw_time_service.c"
        "src/lw_audio.c"
        "src/lw_services.c"
        "src/lw_net_esp.c"
    INCLUDE_DIRS "include"
    # esp_timer is used by the SNTP adapter and by the service clock; it is a
    # plain (non-optional) requirement so the header rule is satisfied on every
    # configuration, including the offline one.
    REQUIRES lw_core esp_timer
)

# The radio and SNTP helpers are only linked when that code is really compiled,
# so the default P4 build stays free of the ESP32-C6 hosted stack.
#
# Note: idf_component_require() does not exist in ESP-IDF 5.5.5 -
# tools/cmake/component.cmake defines idf_component_register(),
# idf_component_mock(), idf_component_optional_requires() and
# idf_component_add_link_dependency() only. The old call here aborted configure
# with "Unknown CMake command idf_component_require".
if(CONFIG_LW_NET_WIFI_ENABLE)
    idf_component_optional_requires(PRIVATE esp_wifi esp_event esp_netif)
    # esp_hosted.h (the coprocessor firmware query) only exists when the hosted
    # overlay installed the component; optional_requires keeps the offline
    # build, which never sees the header, from failing to resolve it.
    # esp_hosted's own public header chain pulls driver/sdmmc_host.h when the
    # SDIO transport is selected, and esp_hosted declares sdmmc PRIVATE, so the
    # include directory has to be named here as well - otherwise the build dies
    # with "fatal error: driver/sdmmc_host.h: No such file or directory".
    if(CONFIG_ESP_HOSTED_ENABLED)
        idf_component_optional_requires(PRIVATE espressif__esp_hosted esp_driver_sdmmc)
    endif()
endif()
if(CONFIG_LW_NET_SNTP_ENABLE)
    idf_component_optional_requires(PRIVATE esp_netif esp_netif_sntp lwip)
endif()
