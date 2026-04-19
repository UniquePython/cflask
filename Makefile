CC = gcc

CFLAGS := -g -Wall -Wextra -Wshadow -Wdouble-promotion -Wformat=2 -Wundef -Wconversion -std=c11 -I include
LDFLAGS := -fsanitize=address,undefined

SRC_DIR = src
BUILD_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET = $(BUILD_DIR)/cflask

.PHONY: all clean run

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJS): | $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)