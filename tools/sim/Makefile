# Renders the gauge page on the Mac (same gauges.c / demo.c / LVGL 8.4 as the firmware).
#   make                     build and render a few moments of the demo loop into out/ as PNG
#   make SHOTS="5 60 130"    pick the moments (seconds into the 3-minute demo loop)
# Needs firmware/managed_components (created by the first idf.py build).
FW    := ../../firmware
LVGL  := $(FW)/managed_components/lvgl__lvgl
SHOTS ?= 8 40 75 130 150
CC    := clang
CFLAGS := -std=gnu11 -O1 -DLV_CONF_INCLUDE_SIMPLE -I. -I$(LVGL) -I$(FW)/main -I$(FW)/components/jd_core/include
LV_SRC := $(shell find $(LVGL)/src -name '*.c')
LV_OBJ := $(patsubst $(LVGL)/%.c,build/lvgl/%.o,$(LV_SRC))
APP_SRC := sim.c $(FW)/main/gauges.c $(FW)/main/demo.c $(wildcard $(FW)/components/jd_core/src/*.c)

.PHONY: all clean
all: build/sim
	@mkdir -p out
	@./build/sim out $(SHOTS) >/dev/null
	@for f in out/*.ppm; do sips -s format png "$$f" --out "$${f%.ppm}.png" >/dev/null && rm -f "$$f"; done
	@ls out/*.png

build/sim: $(LV_OBJ) $(APP_SRC) $(FW)/main/gauges.h $(FW)/main/demo.h lv_conf.h
	$(CC) $(CFLAGS) -Wall -Wextra -Werror -o $@ $(APP_SRC) $(LV_OBJ) -lm

build/lvgl/%.o: $(LVGL)/%.c lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -w -c $< -o $@

clean:
	rm -rf build out
