CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11 -D_DEFAULT_SOURCE -O2 -I./include
LDFLAGS = -pthread

# ---------------------------------------------------------------------------
# Seleccion de plataforma:
#   make            -> backend de SIMULACION (compila y corre en cualquier PC)
#   make PLATFORM=pi -> backend REAL con lgpio + i2c (Raspberry Pi 5)
# ---------------------------------------------------------------------------
ifeq ($(PLATFORM),pi)
CFLAGS  += -DUSE_LGPIO
LDFLAGS += -llgpio -li2c
endif

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
LOG_DIR = logs
TEST_DIR = tests

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

TARGET = rack_monitor

TEST_BINS = $(TEST_DIR)/test_morse $(TEST_DIR)/test_integration

all: $(BUILD_DIR) $(LOG_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LOG_DIR):
	mkdir -p $(LOG_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST_DIR)/test_morse: $(TEST_DIR)/test_morse.c $(SRC_DIR)/morse_auth.c
	$(CC) $(CFLAGS) $^ -o $@

$(TEST_DIR)/test_integration: $(TEST_DIR)/test_integration.c
	$(CC) $(CFLAGS) $< -o $@ -pthread

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f $(TEST_DIR)/test_morse $(TEST_DIR)/test_integration

test-morse: $(TEST_DIR)/test_morse
	./$(TEST_DIR)/test_morse

test-integration: $(TEST_DIR)/test_integration
	./$(TEST_DIR)/test_integration

test: $(TEST_BINS)
	@echo "=== Running Morse Tests ==="
	./$(TEST_DIR)/test_morse
	@echo "=== Running Integration Tests ==="
	./$(TEST_DIR)/test_integration

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean test test-morse test-integration run
