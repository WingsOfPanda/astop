PREFIX ?= /usr/local
CC ?= cc
DCMI_LIBDIR ?= /usr/local/Ascend/driver/lib64/driver
CPPFLAGS += -Iinclude -D_GNU_SOURCE -DHAS_REALLOCARRAY
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra $(shell pkg-config --cflags ncursesw)
LDFLAGS += -Wl,-rpath,$(DCMI_LIBDIR) -Wl,-rpath-link,$(DCMI_LIBDIR)/../common -L$(DCMI_LIBDIR)
LDLIBS += $(shell pkg-config --libs ncursesw) -lm -lpthread -ldcmi
SOURCES := $(wildcard src/*.c)
OBJECTS := $(patsubst src/%.c,build/%.o,$(SOURCES))

.PHONY: all clean install test smoke
all: build/astop

build/astop: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

build:
	mkdir -p $@

install: build/astop
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/astop $(DESTDIR)$(PREFIX)/bin/astop

clean:
	rm -rf build

test: build/astop build/test_core build/test_ui_history build/test_ui_layout build/test_core_empty_dcmi.so
	build/test_core
	build/test_ui_history
	build/test_ui_layout
	test "$$(LD_PRELOAD=$(CURDIR)/build/test_core_empty_dcmi.so build/astop -s)" = '[]'

smoke: build/astop
	python3 tests/smoke.py

build/test_core: tests/test_core.c build/extract_gpuinfo.o build/get_process_info_linux.o build/time.o | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ -lm

build/test_ui_history: tests/test_ui_history.c build/interface_ring_buffer.o build/plot.o | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(shell pkg-config --libs ncursesw) -lm

build/test_ui_layout: tests/test_ui_layout.c build/interface_layout_selection.o build/interface_options.o build/ini.o | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ -lm

build/test_core_empty_dcmi.so: tests/test_core_empty_dcmi.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -shared -fPIC -o $@ $<

-include $(OBJECTS:.o=.d)
