# Target OS: linux, windows, macos (default: detect host)
ifeq ($(TARGET_OS),)
    ifeq ($(OS),Windows_NT)
        TARGET_OS = windows
    else
        UNAME_S := $(shell uname -s)
        ifeq ($(UNAME_S),Darwin)
            TARGET_OS = macos
        else
            TARGET_OS = linux
        endif
    endif
endif

CC  = gcc
ASM = gcc
AR  = ar

ifeq ($(TARGET_OS),windows)
    CC  = x86_64-w64-mingw32-gcc
    ASM = x86_64-w64-mingw32-gcc
    AR  = x86_64-w64-mingw32-ar
endif

CFLAGS   = -Wall -Wextra -Iinclude -Isrc -MMD -MP -O3
ASMFLAGS = -x assembler -c

# Directories
SRC_DIR = src
BUILD_DIR = build/$(TARGET_OS)
TEST_DIR = src/test
TEST_SRC = $(TEST_DIR)/test.c
TEST_BIN = test

# Output library
NAME = $(BUILD_DIR)/libgj_image.a

# Library sources only (exclude tests / X11 display harness)
SRC_C   = $(shell find $(SRC_DIR) -name "*.c" ! -path "$(TEST_DIR)/*")
SRC_ASM = $(shell find $(SRC_DIR) -name "*.s" ! -path "$(TEST_DIR)/*")

OBJ_C   = $(SRC_C:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJ_ASM = $(SRC_ASM:$(SRC_DIR)/%.s=$(BUILD_DIR)/%.o)
OBJ = $(OBJ_C) $(OBJ_ASM)
DEPS = $(OBJ:.o=.d)

.PHONY: all clean test

all: $(NAME)

test: $(NAME)
	$(CC) $(CFLAGS) $(TEST_SRC) -L$(BUILD_DIR) -lgj_image -lX11 -o $(TEST_BIN)

$(NAME): $(OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

clean:
	rm -rf build

-include $(DEPS)
