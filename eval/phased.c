#include "chess.h"

#include <stdlib.h>

#define PHASE_MAX 24

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

static int clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static int sq_file(int sq) {
    return sq % 8;
}

static int sq_rank(int sq) {
    return sq / 8;
}

static int file_rank_to_sq(int file, int rank) {
    return rank * 8 + file;
}

static int mirror_sq(int sq) {
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    return file_rank_to_sq(file, 7 - rank);
}

static int phase_interpolate(int mg_value, int eg_value, int phase) {
    return (mg_value * phase + eg_value * (PHASE_MAX - phase)) / PHASE_MAX;
}

static int game_phase(const Board *board) {
    int phase = PHASE_MAX;
    int sq;

    for (sq = 0; sq < 64; sq++) {
        int piece = abs(board->squares[sq]);
        switch (piece) {
            case WKNIGHT:
            case WBISHOP:
                phase -= 1;
                break;
            case WROOK:
                phase -= 2;
                break;
            case WQUEEN:
                phase -= 4;
                break;
            default:
                break;
        }
    }

    return clamp(phase, 0, PHASE_MAX);
}

static int find_king_sq(const Board *board, int side) {
    int cached = (side == WHITE) ? board->king_sq[0] : board->king_sq[1];
    int sq;
    int target = (side == WHITE) ? WKING : BKING;

    if (cached >= 0 && cached < 64 && board->squares[cached] == target) {
        return cached;
    }

    for (sq = 0; sq < 64; sq++) {
        if (board->squares[sq] == target) {
            return sq;
        }
    }

    return NO_SQUARE;
}

static int file_has_pawn(const Board *board, int file, int side) {
    int rank;

    if (file < 0 || file > 7) {
        return 0;
    }

    for (rank = 0; rank < 8; rank++) {
        int piece = board->squares[file_rank_to_sq(file, rank)];
        if (side == WHITE && piece == WPAWN) {
            return 1;
        }
        if (side == BLACK && piece == BPAWN) {
            return 1;
        }
        if (side == 0 && (piece == WPAWN || piece == BPAWN)) {
            return 1;
        }
    }

    return 0;
}

static int enemy_heavy_on_file(const Board *board, int file, int side) {
    int rank;

    for (rank = 0; rank < 8; rank++) {
        int piece = board->squares[file_rank_to_sq(file, rank)];
        if (side == WHITE && (piece == BROOK || piece == BQUEEN)) {
            return 1;
        }
        if (side == BLACK && (piece == WROOK || piece == WQUEEN)) {
            return 1;
        }
    }

    return 0;
}

static int has_pawn_at(const Board *board, int file, int rank, int side) {
    if (file < 0 || file > 7 || rank < 0 || rank > 7) {
        return 0;
    }

    if (side == WHITE) {
        return board->squares[file_rank_to_sq(file, rank)] == WPAWN;
    }
    return board->squares[file_rank_to_sq(file, rank)] == BPAWN;
}

static int enemy_pawn_controls_sq(const Board *board, int side, int target_sq) {
    int file = sq_file(target_sq);
    int rank = sq_rank(target_sq);

    if (side == WHITE) {
        return has_pawn_at(board, file - 1, rank + 1, BLACK) ||
               has_pawn_at(board, file + 1, rank + 1, BLACK);
    }
    return has_pawn_at(board, file - 1, rank - 1, WHITE) ||
           has_pawn_at(board, file + 1, rank - 1, WHITE);
}

static int is_passed_pawn(const Board *board, int side, int sq) {
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int f;
    int r;

    for (f = file - 1; f <= file + 1; f++) {
        if (f < 0 || f > 7) {
            continue;
        }
        if (side == WHITE) {
            for (r = rank + 1; r < 8; r++) {
                if (board->squares[file_rank_to_sq(f, r)] == BPAWN) {
                    return 0;
                }
            }
        } else {
            for (r = rank - 1; r >= 0; r--) {
                if (board->squares[file_rank_to_sq(f, r)] == WPAWN) {
                    return 0;
                }
            }
        }
    }

    return 1;
}

static int is_backward_pawn(const Board *board, int side, int sq) {
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int step = (side == WHITE) ? 1 : -1;
    int next_rank = rank + step;
    int advance_sq;
    int no_support;

    if (next_rank < 0 || next_rank > 7) {
        return 0;
    }

    if (is_passed_pawn(board, side, sq)) {
        return 0;
    }

    advance_sq = file_rank_to_sq(file, next_rank);
    no_support = !has_pawn_at(board, file - 1, rank, side) && !has_pawn_at(board, file + 1, rank, side);

    if (!no_support) {
        return 0;
    }

    if (board->squares[advance_sq] != EMPTY) {
        return 1;
    }

    return enemy_pawn_controls_sq(board, side, advance_sq);
}

static int pawn_structure_side(const Board *board, int side, int phase) {
    int pawns_on_file[8] = {0};
    int sq;
    int score = 0;

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];
        if ((side == WHITE && piece == WPAWN) || (side == BLACK && piece == BPAWN)) {
            pawns_on_file[sq_file(sq)]++;
        }
    }

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];
        if ((side == WHITE && piece == WPAWN) || (side == BLACK && piece == BPAWN)) {
            int file = sq_file(sq);
            int rank = sq_rank(sq);
            int left_count = (file > 0) ? pawns_on_file[file - 1] : 0;
            int right_count = (file < 7) ? pawns_on_file[file + 1] : 0;
            int isolated = (left_count == 0 && right_count == 0);
            int doubled = pawns_on_file[file] > 1;

            if (isolated) {
                score -= phase_interpolate(14, 8, phase);
            }
            if (doubled) {
                score -= phase_interpolate(12, 8, phase);
            }
            if (is_backward_pawn(board, side, sq)) {
                score -= phase_interpolate(12, 6, phase);
            }

            if (is_passed_pawn(board, side, sq)) {
                int advance = (side == WHITE) ? rank : (7 - rank);
                score += phase_interpolate(8 + advance * 5, 14 + advance * 8, phase);
            }
        }
    }

    return score;
}

static int king_safety_side(const Board *board, int side, int phase) {
    int king_sq = find_king_sq(board, side);
    int file;
    int rank;
    int f;
    int score = 0;

    if (king_sq == NO_SQUARE) {
        return 0;
    }

    file = sq_file(king_sq);
    rank = sq_rank(king_sq);

    if ((side == WHITE && (king_sq == 6 || king_sq == 2)) ||
        (side == BLACK && (king_sq == 62 || king_sq == 58))) {
        score += 30;
    }

    if (file >= 2 && file <= 5 && rank >= 2 && rank <= 5) {
        score -= 24;
    }

    for (f = file - 1; f <= file + 1; f++) {
        if (f < 0 || f > 7) {
            continue;
        }

        if (!file_has_pawn(board, f, side)) {
            score -= 12;
        }
        if (!file_has_pawn(board, f, 0)) {
            score -= 8;
            if (enemy_heavy_on_file(board, f, side)) {
                score -= 12;
            }
        }
    }

    if (side == WHITE) {
        int rf;
        for (rf = file - 1; rf <= file + 1; rf++) {
            if (rf < 0 || rf > 7) {
                continue;
            }
            if (!has_pawn_at(board, rf, rank + 1, WHITE)) {
                score -= 10;
            }
            if (!has_pawn_at(board, rf, rank + 2, WHITE)) {
                score -= 4;
            }
        }
    } else {
        int rf;
        for (rf = file - 1; rf <= file + 1; rf++) {
            if (rf < 0 || rf > 7) {
                continue;
            }
            if (!has_pawn_at(board, rf, rank - 1, BLACK)) {
                score -= 10;
            }
            if (!has_pawn_at(board, rf, rank - 2, BLACK)) {
                score -= 4;
            }
        }
    }

    return (score * phase) / PHASE_MAX;
}

static const int pawn_pst[64] = {
      0,   0,   0,   0,   0,   0,  0,   0,
      5,  10,  10, -20, -20,  10, 10,   5,
      5,  -5, -10,   0,   0, -10, -5,   5,
      0,   0,   0,  24,  24,   0,  0,   0,
      5,   5,  10,  28,  28,  10,  5,   5,
     10,  10,  20,  30,  30,  20, 10,  10,
     30,  30,  30,  35,  35,  30, 30,  30,
      0,   0,   0,   0,   0,   0,  0,   0
};

static const int knight_pst[64] = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -30,   5,  12,  15,  15,  12,   5, -30,
    -30,   0,  15,  24,  24,  15,   0, -30,
    -30,   0,  15,  24,  24,  15,   0, -30,
    -30,   5,  12,  15,  15,  12,   5, -30,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

static const int king_mg_pst[64] = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,   0,   0,   0,   0,  20,  20,
     20,  30,  10,   0,   0,  10,  30,  20
};

static const int king_eg_pst[64] = {
    -50, -30, -20, -20, -20, -20, -30, -50,
    -30, -10,   0,   0,   0,   0, -10, -30,
    -20,   0,  10,  15,  15,  10,   0, -20,
    -20,   0,  15,  25,  25,  15,   0, -20,
    -20,   0,  15,  25,  25,  15,   0, -20,
    -20,   0,  10,  15,  15,  10,   0, -20,
    -30, -10,   0,   0,   0,   0, -10, -30,
    -50, -30, -20, -20, -20, -20, -30, -50
};

static int piece_square_value(int piece, int sq, int phase) {
    int abs_piece = abs(piece);
    int index = (piece > 0) ? sq : mirror_sq(sq);

    switch (abs_piece) {
        case WPAWN:
            return pawn_pst[index];
        case WKNIGHT:
            return knight_pst[index];
        case WKING:
            return phase_interpolate(king_mg_pst[index], king_eg_pst[index], phase);
        default:
            return 0;
    }
}

int evaluate_board_phased(const Board *board) {
    int phase = game_phase(board);
    int score = 0;
    int sq;
    Board temp;
    Move moves[MAX_MOVES];
    int white_mobility;
    int black_mobility;
    int mobility_weight;

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];

        if (piece == EMPTY) {
            continue;
        }

        if (piece > 0) {
            score += piece_value(piece);
            score += piece_square_value(piece, sq, phase);
        } else {
            score -= piece_value(piece);
            score -= piece_square_value(piece, sq, phase);
        }
    }

    score += pawn_structure_side(board, WHITE, phase);
    score -= pawn_structure_side(board, BLACK, phase);

    score += king_safety_side(board, WHITE, phase);
    score -= king_safety_side(board, BLACK, phase);

    temp = *board;
    temp.side_to_move = WHITE;
    white_mobility = generate_legal_moves(&temp, moves);
    temp.side_to_move = BLACK;
    black_mobility = generate_legal_moves(&temp, moves);

    mobility_weight = phase_interpolate(3, 1, phase);
    score += (white_mobility - black_mobility) * mobility_weight;

    return score;
}
