# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g -O2
LDFLAGS = -pthread

# Source files
SERVER_SOURCES = server.c queue.c user_management.c file_operations.c command_processing.c
CLIENT_SOURCES = client.c

# Object files
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# Executables
SERVER_TARGET = server
CLIENT_TARGET = client

# Default target
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Server executable
$(SERVER_TARGET): $(SERVER_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

# Client executable
$(CLIENT_TARGET): $(CLIENT_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

# Object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(SERVER_OBJECTS) $(CLIENT_OBJECTS) $(SERVER_TARGET) $(CLIENT_TARGET)
	rm -rf users/

# Install dependencies (for Ubuntu/Debian)
install-deps:
	sudo apt-get update
	sudo apt-get install -y build-essential valgrind

# Run with valgrind
valgrind: $(SERVER_TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(SERVER_TARGET)

# Run with thread sanitizer
tsan: CFLAGS += -fsanitize=thread
tsan: LDFLAGS += -fsanitize=thread
tsan: clean $(SERVER_TARGET)
	./$(SERVER_TARGET)

# Test
test: $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "Starting server in background..."
	./$(SERVER_TARGET) &
	SERVER_PID=$$!; \
	sleep 2; \
	echo "Testing client..."; \
	echo -e "SIGNUP testuser testpass\nLOGIN testuser testpass\nLIST\nLOGOUT testuser\nquit" | ./$(CLIENT_TARGET); \
	kill $$SERVER_PID 2>/dev/null; \
	wait $$SERVER_PID 2>/dev/null

.PHONY: all clean install-deps valgrind tsan test
