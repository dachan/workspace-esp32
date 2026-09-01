idf_component_register(
    SRCS "main.c" "ld2450.c" "radar_link.c" "radar_view.c"
         "../../hardware-test/main/board_rgb_led.c"
         "../../hardware-test/main/color_palette.c"
         "../../hardware-test/main/display_sync.c"
         "../../hardware-test/main/joystick.c"
         "../../hardware-test/main/microphone.c"
         "../../hardware-test/main/rainbow_wave.c"
         "../../hardware-test/main/speech_recognition.c"
         "../../hardware-test/main/touch_calibration.c"
    INCLUDE_DIRS "." "../../hardware-test/main"
    REQUIRES driver esp_adc esp_driver_i2c esp_driver_i2s esp_driver_rmt esp_driver_sdspi esp_driver_uart esp_event esp_lcd esp_netif esp_psram esp_timer esp_wifi espressif__esp-sr fatfs nvs_flash sdmmc spi_flash
)

set(DISPLAY_PROFILE "ili9341_3_2" CACHE STRING "Display profile to compile")
set_property(CACHE DISPLAY_PROFILE PROPERTY STRINGS ili9341_2_8 ili9341_3_2 st7796u_3_5)

set(RADAR_LINK_ROLE "local" CACHE STRING "Radar data role")
set_property(CACHE RADAR_LINK_ROLE PROPERTY STRINGS local transmitter receiver)

if(RADAR_LINK_ROLE STREQUAL "transmitter")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RADAR_LINK_ROLE_TRANSMITTER)
elseif(RADAR_LINK_ROLE STREQUAL "receiver")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RADAR_LINK_ROLE_RECEIVER)
elseif(NOT RADAR_LINK_ROLE STREQUAL "local")
    message(FATAL_ERROR "Unsupported RADAR_LINK_ROLE: ${RADAR_LINK_ROLE}")
endif()

if(DISPLAY_PROFILE STREQUAL "ili9341_2_8")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DISPLAY_PROFILE_ILI9341_2_8)
elseif(DISPLAY_PROFILE STREQUAL "ili9341_3_2")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DISPLAY_PROFILE_ILI9341_3_2)
elseif(DISPLAY_PROFILE STREQUAL "st7796u_3_5")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DISPLAY_PROFILE_ST7796U_3_5)
else()
    message(FATAL_ERROR "Unsupported DISPLAY_PROFILE: ${DISPLAY_PROFILE}")
endif()
