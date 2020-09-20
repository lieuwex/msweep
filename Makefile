CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
BIN = msweep
TARGETS = $(BIN) antest

.PHONY: all clean install uninstall make_in_analysis

all: make_in_analysis $(TARGETS)

clean:
	rm -f $(TARGETS)
	+make --no-print-directory -C analysis clean

install: $(BIN)
	install $(BIN) /usr/local/bin

uninstall: $(BIN)
	rm -f /usr/local/bin/$(BIN)

make_in_analysis:
	+make --no-print-directory -C analysis


msweep: msweep.c
	$(CC) $(CFLAGS) -o $@ $^

antest: antest.c analysis/libanalysis.a
	$(CC) $(CFLAGS) -o $@ $^ analysis/libanalysis.a
