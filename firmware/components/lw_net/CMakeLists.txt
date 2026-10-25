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
    REQUIRES lw_core
)

# esp_wifi and the SNTP helper are only linked when the radio is actually
# enabled, so the default P4 build has no dependency on the C6 hosted stack.
if(CONFIG_LW_NET_WIFI_ENABLE)
    idf_component_require(lw_net esp_wifi esp_event esp_netif)
endif()
if(CONFIG_LW_NET_SNTP_ENABLE)
    idf_component_require(lw_net esp_netif)
endif()
