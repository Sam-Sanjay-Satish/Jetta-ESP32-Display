// LVGL 8.4 config for the Mac screen renderer. Matches the firmware's sdkconfig.defaults
// (16-bit colour, 32 KB pool, the same fonts); byte swap off because we write a PNG, not SPI.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_SIZE (32U * 1024U)
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#endif
#endif
