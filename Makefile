CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -pthread
LDFLAGS = -pthread
LIBS = -lsqlite3

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin

SERVER_SRCS = \
  $(SRC_DIR)/server.c \
  $(SRC_DIR)/queue.c \
  $(SRC_DIR)/threadpool.c \
  $(SRC_DIR)/lockmgr.c \
  $(SRC_DIR)/db.c \
  $(SRC_DIR)/util.c

CLIENT_SRCS = \
  $(SRC_DIR)/client.c \
  $(SRC_DIR)/util.c

SERVER_OBJS = $(SERVER_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CLIENT_OBJS = $(CLIENT_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

ALL_DIRS = $(BUILD_DIR) $(BIN_DIR) storage

.PHONY: all clean debug tsan run test smoke concurrency valgrind tsan-test

all: dirs $(BIN_DIR)/server $(BIN_DIR)/client

dirs:
	@mkdir -p $(ALL_DIRS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(SRC_DIR) -c $< -o $@

$(BIN_DIR)/server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

$(BIN_DIR)/client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

debug: CFLAGS = -Wall -Wextra -Werror -O0 -g -pthread
debug: clean all

tsan: CFLAGS = -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=thread -pthread
tsan: LDFLAGS = -fsanitize=thread -pthread
tsan: clean all

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

run: all
	$(BIN_DIR)/server --port 9000 --root storage --quota-bytes 104857600

test: all
	bash tests/smoke.sh

smoke: all
	bash tests/smoke.sh

concurrency: all
	bash tests/concurrency.sh

valgrind: all
	@bash tests/valgrind_server.sh

tsan-test:
	bash tests/tsan_test.sh


