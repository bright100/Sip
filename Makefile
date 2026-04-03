CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c17 -Wno-format-truncation
IFLAGS  = -Icpm/include
LDFLAGS = -lcurl

SRCS =  cpm/src/main.c \
        cpm/src/toml.c \
        cpm/src/registry.c \
        cpm/src/resolver.c \
        cpm/src/core/utils.c \
        cpm/src/core/manifest.c \
        cpm/src/commands/cmd_init.c \
        cpm/src/commands/cmd_add.c \
        cpm/src/commands/cmd_install.c \
        cpm/src/commands/cmd_build.c \
        cpm/src/commands/cmd_run.c \
        cpm/src/commands/cmd_remove.c \
        cpm/src/commands/cmd_update.c \
        cpm/src/commands/cmd_publish.c

TARGET  = cpm/bin/cpm

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p cpm/bin
	$(CC) $(CFLAGS) $(IFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
