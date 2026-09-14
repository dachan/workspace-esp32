idf_component_register(
    SRCS "main.c" "st7735.c" "ssd1306.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash
)
