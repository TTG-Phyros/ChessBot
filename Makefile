CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
SRC = src/board.c src/movegen.c src/eval.c src/minimax.c
MHD_CFLAGS = $(shell pkg-config --cflags libmicrohttpd json-c 2> /dev/null)
MHD_LIBS = $(shell pkg-config --libs libmicrohttpd json-c 2> /dev/null)
CURL_CFLAGS = $(shell pkg-config --cflags libcurl 2> /dev/null)
CURL_LIBS = $(shell pkg-config --libs libcurl 2> /dev/null)

all: chess-bot

chess-bot: $(SRC) src/main.c include/chess.h
	$(CC) $(CFLAGS) -o $@ $(SRC) src/main.c

chess-api: $(SRC) src/api.c include/chess.h
	$(CC) $(CFLAGS) $(MHD_CFLAGS) $(CURL_CFLAGS) -o $@ src/api.c $(MHD_LIBS) $(CURL_LIBS)

chess-bot-service: $(SRC) src/bot_service.c include/chess.h
	$(CC) $(CFLAGS) $(MHD_CFLAGS) -o $@ src/bot_service.c $(MHD_LIBS)

test: $(SRC) tests/test_minimax.c include/chess.h
	$(CC) $(CFLAGS) -o test-minimax $(SRC) tests/test_minimax.c
	./test-minimax

test-perft: $(SRC) tests/test_perft.c include/chess.h
	$(CC) $(CFLAGS) -o test-perft $(SRC) tests/test_perft.c
	./test-perft

clean:
	rm -f chess-bot chess-api chess-bot-service test-minimax test-perft

.PHONY: all test test-perft clean
