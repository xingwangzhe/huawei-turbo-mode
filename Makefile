CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
PREFIX ?= /usr/local
CORE := core

all:
	$(MAKE) -C $(CORE)

test: all
	$(MAKE) -C $(CORE) test
	python3 -m unittest discover -s tests -v

install: all
	./scripts/install.sh

clean:
	$(MAKE) -C $(CORE) clean

.PHONY: all test install clean
