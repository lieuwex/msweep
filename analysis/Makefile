CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -O2 -std=c99
LIB = libanalysis.a

.PHONY: all clean

all: $(LIB)

clean:
	rm -f $(LIB) *.o


$(LIB): $(patsubst %.c,%.o,$(wildcard *.c))
	$(AR) -cr $@ $^

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c -o $@ $<
