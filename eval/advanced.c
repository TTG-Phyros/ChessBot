#include "chess.h"

#include <stdlib.h>

static int piece_value(int piece) {
    switch (abs(piece)) {
        case WPAWN: return 100;
        case WKNIGHT: return 320;
        case WBISHOP: return 330;
        case WROOK: return 500;
        case WQUEEN: return 900;
        case WKING: return 20000;
        default: return 0;
    }
}

static int center_bonus(int sq) {
    int file = sq % 8;
    int rank = sq / 8;

    if (file >= 2 && file <= 5 && rank >= 2 && rank <= 5) {
        return 12;
    }
    if (file >= 1 && file <= 6 && rank >= 1 && rank <= 6) {
        return 6;
    }
    return 0;
}

static int positional_bonus(int piece, int sq) {
    int abs_piece = abs(piece);
    int bonus = 0;

    if (abs_piece == WPAWN) {
        bonus += center_bonus(sq);
    } else if (abs_piece == WKNIGHT || abs_piece == WBISHOP) {
        bonus += center_bonus(sq) * 2;
    } else if (abs_piece == WQUEEN) {
        bonus += center_bonus(sq);
    }

    return bonus;
}

int evaluate_board_advanced(const Board *board) {
    int score = 0;
    int sq;
    Move moves[MAX_MOVES];
    Board temp;
    int white_mobility;
    int black_mobility;

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];
        int abs_val = piece_value(piece);
        int pos_val = positional_bonus(piece, sq);

        if (piece > 0) {
            score += abs_val + pos_val;
        } else if (piece < 0) {
            score -= abs_val + pos_val;
        }
    }

    temp = *board;
    temp.side_to_move = WHITE;
    white_mobility = generate_legal_moves(&temp, moves);
    temp.side_to_move = BLACK;
    black_mobility = generate_legal_moves(&temp, moves);

    score += (white_mobility - black_mobility) * 2;

    return score;
}
