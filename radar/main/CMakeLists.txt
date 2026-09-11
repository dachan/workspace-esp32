set(RADAR_LINK_ROLE "transmitter" CACHE STRING "Radar board role")
set_property(CACHE RADAR_LINK_ROLE PROPERTY STRINGS transmitter receiver)
set(RADAR_BOARD "default" CACHE STRING "Radar hardware board profile")
set_property(CACHE RADAR_BOARD PROPERTY STRINGS default supermini)

if(RADAR_LINK_ROLE STREQUAL "transmitter")
    idf_component_register(
        SRCS "main.c" "ld2450.c" "radar_link.c" "radar_motion_leds.c"
        INCLUDE_DIRS "."
        REQUIRES driver esp_driver_gpio esp_driver_rmt esp_driver_uart esp_event esp_netif esp_wifi nvs_flash
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RADAR_LINK_ROLE_TRANSMITTER)
    if(RADAR_BOARD STREQUAL "supermini")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE RADAR_BOARD_SUPERMINI=1)
    endif()
elseif(RADAR_LINK_ROLE STREQUAL "receiver")
    if(RADAR_BOARD STREQUAL "supermini")
        message(FATAL_ERROR "RADAR_BOARD=supermini is only supported for the transmitter")
    endif()
    idf_component_register(
        SRCS "receiver_main.c" "receiver_touch.c" "radar_link.c" "radar_view.c"
             "../../hardware-test/main/display_sync.c"
             "../../hardware-test/main/touch_calibration.c"
        INCLUDE_DIRS "." "../../hardware-test/main"
        REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_spi esp_event esp_lcd esp_netif esp_wifi
                 espressif__esp_lcd_ili9341 nvs_flash
    )
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RADAR_LINK_ROLE_RECEIVER
                               DISPLAY_PROFILE_ILI9341_2_8)
else()
    message(FATAL_ERROR "Unsupported RADAR_LINK_ROLE: ${RADAR_LINK_ROLE}")
endif()
