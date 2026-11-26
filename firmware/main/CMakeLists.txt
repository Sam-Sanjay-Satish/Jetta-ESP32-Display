idf_component_register(
    SRCS "app_main.c" "can_twai.c" "lcd.c" "ui.c" "gauges.c" "pages.c" "touch.c" "demo.c" "sysmon.c"
    INCLUDE_DIRS "."
    REQUIRES jd_core driver esp_lcd esp_driver_spi esp_driver_gpio esp_driver_i2c esp_timer freertos)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
