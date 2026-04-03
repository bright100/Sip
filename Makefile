CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c17
INCLUDES = -Icpm/include
SRCS = cpm/src/main.c cpm/src/registry.c cpm/src/resolver.c cpm/src/toml.c
TARGET = cpm/bin/cpm

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
