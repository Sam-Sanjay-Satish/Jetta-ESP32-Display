# Host-side tests for jd_core (runs on the Mac, no ESP32 needed).
#   make        build and run every test_*.c
#   make clean
CORE    := ../../firmware/components/jd_core
CC      := clang
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes -Werror \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I$(CORE)/include -Iunity -DUNITY_INCLUDE_DOUBLE
LDFLAGS := -fsanitize=address,undefined

CORE_SRC := $(wildcard $(CORE)/src/*.c)
TESTS    := $(patsubst %.c,build/%,$(wildcard test_*.c))

.PHONY: all clean
all: $(TESTS)
	@fail=0; for t in $(TESTS); do echo "== $$t"; ./$$t || fail=1; done; \
	if [ $$fail = 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; exit 1; fi

build/%: %.c $(CORE_SRC) unity/unity.c $(wildcard $(CORE)/include/jd/*.h) test_util.h
	@mkdir -p build
	@echo "CC $@"; $(CC) $(CFLAGS) -o $@ $< $(CORE_SRC) unity/unity.c $(LDFLAGS)

clean:
	rm -rf build
