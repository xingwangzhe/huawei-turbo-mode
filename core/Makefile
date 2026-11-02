CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
PREFIX ?= /usr/local

BUILD := build

all: $(BUILD)/huawei-perf $(BUILD)/burn

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/huawei-perf: src/huawei_perf.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD)/burn: src/burn.c | $(BUILD)
	$(CC) $(CFLAGS) -mavx2 -mfma -pthread -o $@ $<

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BUILD)/huawei-perf $(DESTDIR)$(PREFIX)/bin/huawei-perf
	install -m 0755 $(BUILD)/burn $(DESTDIR)$(PREFIX)/bin/huawei-perf-burn

test: all
	./$(BUILD)/huawei-perf selftest
	./$(BUILD)/burn 1 2

clean:
	rm -rf $(BUILD)

.PHONY: all install test clean
