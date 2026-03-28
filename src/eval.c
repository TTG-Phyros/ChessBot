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

int evaluate_board(const Board *board) {
    int score = 0;
    int sq;

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];
        if (piece > 0) {
            score += piece_value(piece);
        } else if (piece < 0) {
            score -= piece_value(piece);
        }
    }

    return score;
}
