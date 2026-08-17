# Compiler
CC  = gcc
ASM = gcc

# Output library
NAME = libgj_image.a

# Flags
CFLAGS   = -Wall -Wextra -Iinclude -Isrc -MMD -MP -O3
ASMFLAGS = -x assembler -c

LDFLAGS  = -lX11

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = src/test
TEST_SRC = $(TEST_DIR)/test.c
TEST_BIN = test

# Find all source files (internal only)
SRC_C   = $(shell find $(SRC_DIR) -name "*.c")
SRC_ASM = $(shell find $(SRC_DIR) -name "*.s")

# Object files (mirror structure in build/)
OBJ_C   = $(SRC_C:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJ_ASM = $(SRC_ASM:$(SRC_DIR)/%.s=$(BUILD_DIR)/%.o)

OBJ = $(OBJ_C) $(OBJ_ASM)

# For tracking header dependencies
DEPS = $(OBJ:.o=.d)

# Default target
all: $(NAME)

test: $(NAME)
	$(CC) $(CFLAGS) $(TEST_SRC) -L. -lgj_image $(LDFLAGS) -o $(TEST_BIN)

# Build static library
$(NAME): $(OBJ)
	ar rcs $@ $^

# Compile objects
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ASM
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR) $(NAME)

-include $(DEPS)

.PHONY: all clean
