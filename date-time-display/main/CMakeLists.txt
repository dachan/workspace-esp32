idf_component_register(
    SRCS "main.c" "clock_sync.c" "st7735.c" "ssd1306.c" "indicator_led.c" "joystick.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_driver_rmt esp_driver_spi esp_driver_usb_serial_jtag
             esp_event esp_netif esp_wifi lwip nvs_flash
)
