CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude

TARGET_TEST = test_netstack
TARGET_SERVER = server

SRC_NETSTACK = src/netstack.c
SRC_TEST = src/test_netstack.c
SRC_SERVER = src/main.c

.PHONY: all clean run-test

all: $(TARGET_TEST) $(TARGET_SERVER)

$(TARGET_TEST): $(SRC_TEST) $(SRC_NETSTACK)
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET_SERVER): $(SRC_SERVER) $(SRC_NETSTACK)
	$(CC) $(CFLAGS) -o $@ $^ -lssl -lcrypto

run-test: $(TARGET_TEST)
	sudo ./$(TARGET_TEST)

clean:
	rm -f $(TARGET_TEST) $(TARGET_SERVER) *.o
