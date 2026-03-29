CC = gcc
PYTHON ?= python
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
EVAL_SRC = eval/basic.c eval/advanced.c eval/tactical.c eval/phased.c eval/select.c
SRC = src/board.c src/movegen.c $(EVAL_SRC)
BOT_MINIMAX_SRC = src/board.c src/movegen.c bots/minimax/src/minimax.c $(EVAL_SRC)
BOT_ITERDEEP_SRC = src/board.c src/movegen.c bots/iterative-deepening/src/iterdeep.c $(EVAL_SRC)
BOT_MINIMAX_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -fopenmp -Iinclude
BOT_ITERDEEP_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -fopenmp -Iinclude
MHD_CFLAGS = $(shell pkg-config --cflags libmicrohttpd json-c 2> /dev/null)
MHD_LIBS = $(shell pkg-config --libs libmicrohttpd json-c 2> /dev/null)
CURL_CFLAGS = $(shell pkg-config --cflags libcurl 2> /dev/null)
CURL_LIBS = $(shell pkg-config --libs libcurl 2> /dev/null)

all: chess-bot chess-bot-iterdeep

chess-bot: $(BOT_MINIMAX_SRC) bots/minimax/src/main.c include/chess.h
	$(CC) $(BOT_MINIMAX_CFLAGS) -o $@ $(BOT_MINIMAX_SRC) bots/minimax/src/main.c

chess-bot-iterdeep: $(BOT_ITERDEEP_SRC) bots/iterative-deepening/src/main.c include/chess.h
	$(CC) $(BOT_ITERDEEP_CFLAGS) -o $@ $(BOT_ITERDEEP_SRC) bots/iterative-deepening/src/main.c

chess-api: src/api.c
	$(CC) $(CFLAGS) $(MHD_CFLAGS) $(CURL_CFLAGS) -o $@ src/api.c $(MHD_LIBS) $(CURL_LIBS)

chess-bot-service: src/bot_service.c
	$(CC) $(CFLAGS) $(MHD_CFLAGS) -o $@ src/bot_service.c $(MHD_LIBS)

test-engine-core-minimax: $(BOT_MINIMAX_SRC) bots/minimax/tests/test_engine_core.c include/chess.h
	$(CC) $(BOT_MINIMAX_CFLAGS) -o $@ $(BOT_MINIMAX_SRC) bots/minimax/tests/test_engine_core.c

test-perft-minimax: $(BOT_MINIMAX_SRC) bots/minimax/tests/test_perft.c include/chess.h
	$(CC) $(BOT_MINIMAX_CFLAGS) -o $@ $(BOT_MINIMAX_SRC) bots/minimax/tests/test_perft.c

test-engine-core-iterdeep: $(BOT_ITERDEEP_SRC) bots/iterative-deepening/tests/test_engine_core.c include/chess.h
	$(CC) $(BOT_ITERDEEP_CFLAGS) -o $@ $(BOT_ITERDEEP_SRC) bots/iterative-deepening/tests/test_engine_core.c

test-perft-iterdeep: $(BOT_ITERDEEP_SRC) bots/iterative-deepening/tests/test_perft.c include/chess.h
	$(CC) $(BOT_ITERDEEP_CFLAGS) -o $@ $(BOT_ITERDEEP_SRC) bots/iterative-deepening/tests/test_perft.c

test: test-engine-core-minimax test-engine-core-iterdeep
	./test-engine-core-minimax
	./test-engine-core-iterdeep

test-perft: test-perft-minimax test-perft-iterdeep
	./test-perft-minimax
	./test-perft-iterdeep

elo-eval: all
	$(PYTHON) tools/elo_eval.py --bot=minimax --bot-depth=4 --stockfish=stockfish --levels=1320,1500,1700,1900 --games-per-level=8

clean:
	rm -f chess-bot chess-bot-iterdeep chess-api chess-bot-service test-engine-core-minimax test-perft-minimax test-engine-core-iterdeep test-perft-iterdeep

.PHONY: all test test-perft elo-eval clean
