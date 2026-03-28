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

static int tactical_piece_bonus(int piece) {
    switch (abs(piece)) {
        case WPAWN: return 0;
        case WKNIGHT: return 12;
        case WBISHOP: return 12;
        case WROOK: return 8;
        case WQUEEN: return 6;
        case WKING: return 0;
        default: return 0;
    }
}

static int square_attack_count(const Board *board, int target_sq, int side) {
    Move moves[MAX_MOVES];
    Board temp = *board;
    int count;
    int i;
    int attacks = 0;

    temp.side_to_move = side;
    count = generate_legal_moves(&temp, moves);
    for (i = 0; i < count; i++) {
        if (moves[i].to == target_sq) {
            attacks++;
        }
    }

    return attacks;
}

int evaluate_board_tactical(const Board *board) {
    int score = 0;
    int sq;

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];

        if (piece == EMPTY) {
            continue;
        }

        if (piece > 0) {
            score += piece_value(piece);
            score += tactical_piece_bonus(piece);
            score += square_attack_count(board, sq, WHITE) * 2;
            score -= square_attack_count(board, sq, BLACK) * 2;
        } else {
            score -= piece_value(piece);
            score -= tactical_piece_bonus(piece);
            score -= square_attack_count(board, sq, BLACK) * 2;
            score += square_attack_count(board, sq, WHITE) * 2;
        }
    }

    return score;
}
