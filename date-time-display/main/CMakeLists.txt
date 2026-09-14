idf_component_register(
    SRCS "main.c" "st7735.c" "ssd1306.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_driver_spi nvs_flash
)
