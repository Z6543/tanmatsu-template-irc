idf_build_get_property(idf_target IDF_TARGET)
if("${idf_target}" STREQUAL "esp32p4")
    list(APPEND extra_requires esp_driver_ppa)
endif()

idf_component_register(
    SRCS
        "main.c"
        "bsp_lvgl.c"
        "sdcard.c"
        "irc_client.c"
        "irc_ui.c"
        "irc_menu.c"
        "irc_settings.c"
    PRIV_REQUIRES
        esp_lcd
        esp_timer
        driver
        esp_psram
        fatfs
        sdmmc
        esp_driver_sdmmc
        nvs_flash
        badge-bsp
        tanmatsu-wifi
        nicolaielectronics__wifi-manager
        esp_wifi
        lwip
        esp-tls
        ${extra_requires}
    INCLUDE_DIRS
        "."
)

idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
idf_component_get_property(libpng_lib espressif__libpng COMPONENT_LIB)
target_link_libraries(${lvgl_lib} PUBLIC ${libpng_lib})
