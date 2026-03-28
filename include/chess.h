#ifndef CHESS_H
#define CHESS_H

#include <stddef.h>

#define MAX_MOVES 256
#define CHECKMATE_SCORE 100000

#define NO_SQUARE -1

#define WHITE 1
#define BLACK -1

#define EMPTY 0

#define WPAWN 1
#define WKNIGHT 2
#define WBISHOP 3
#define WROOK 4
#define WQUEEN 5
#define WKING 6

#define BPAWN -1
#define BKNIGHT -2
#define BBISHOP -3
#define BROOK -4
#define BQUEEN -5
#define BKING -6

#define CASTLE_WHITE_K 0x1
#define CASTLE_WHITE_Q 0x2
#define CASTLE_BLACK_K 0x4
#define CASTLE_BLACK_Q 0x8

#define MOVE_FLAG_NONE 0x0
#define MOVE_FLAG_EN_PASSANT 0x1
#define MOVE_FLAG_CASTLE_KING 0x2
#define MOVE_FLAG_CASTLE_QUEEN 0x4

typedef struct {
    int from;
    int to;
    int promotion;
    int captured;
    int flags;
} Move;

typedef struct {
    int squares[64];
    int side_to_move;
    int castling_rights;
    int en_passant_sq;
    int king_sq[2];  /* Cached king positions: [0]=WHITE, [1]=BLACK */
} Board;

void board_set_startpos(Board *board);
int board_from_fen(Board *board, const char *fen);
void board_print(const Board *board);
void move_to_uci(const Move *move, char out[6]);
int parse_uci_move(const Board *board, const char *uci, Move *out_move);

int generate_legal_moves(const Board *board, Move moves[MAX_MOVES]);
void apply_move(const Board *board, const Move *move, Board *out_board);
int is_in_check(const Board *board, int side);

int evaluate_board(const Board *board);
int choose_best_move(const Board *board, int depth, Move *best_move);

#endif
