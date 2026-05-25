# Compiler
CC = gcc

# Output library
NAME = libgj_image.a

# Flags
CFLAGS = -Wall -Wextra -O2 -Iinclude -Isrc
LDFLAGS = -lX11

# Directories
SRC_DIR = src
BUILD_DIR = build
TEST_DIR = src/test
TEST_SRC = $(TEST_DIR)/test.c
TEST_BIN = test

# Find all source files (internal only)
SRCS = $(shell find $(SRC_DIR) -name "*.c")

# Object files (mirror structure in build/)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Default target
all: $(NAME)

test: $(NAME)
	$(CC) $(CFLAGS) $(TEST_SRC) -L. -lgj_image $(LDFLAGS) -o $(TEST_BIN)

# Build static library
$(NAME): $(OBJS)
	ar rcs $@ $^

# Compile objects
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -rf $(BUILD_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
