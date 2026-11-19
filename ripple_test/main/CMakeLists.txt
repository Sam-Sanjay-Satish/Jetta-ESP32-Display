idf_component_register(
    SRCS "main.c" "display_ili9341.c" "touch_ft6336.c"
    INCLUDE_DIRS "."
    REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio esp_timer)
