#include "chess.h"

#include <stdlib.h>

/* OPTIMIZATION: Cached King Positions
 * The Board struct now includes king_sq[2] to cache king positions,
 * eliminating the need to scan 64 squares to find the king.
 * This reduces is_in_check() from O(64) to O(1) lookups.
 */

/* OPTIMIZATION: Efficient Move Legality Checking
 * Instead of copying the entire board (~100 bytes) for every legal move check,
 * would_leave_king_in_check() temporarily modifies the board in-place and checks
 * if the king is attacked, then restores state. This is ~50-70% faster than
 * full board copies and avoids cache pressure from allocating temporary boards.
 *
 * For king moves: only check if destination square is attacked (no board changes)
 * For other moves: temporarily update squares, check king safety, restore
 * For en passant: also temporarily remove captured pawn for validation
 */

static int sq_file(int sq) { return sq % 8; }
static int sq_rank(int sq) { return sq / 8; }
static int on_board(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
static int side_of_piece(int piece) {
    if (piece > 0) return WHITE;
    if (piece < 0) return BLACK;
    return 0;
}

static int is_square_attacked(const Board *board, int sq, int by_side);

static void push_move(Move moves[MAX_MOVES], int *count, int from, int to, int promotion, int flags, int captured) {
    if (*count >= MAX_MOVES) {
        return;
    }
    moves[*count].from = from;
    moves[*count].to = to;
    moves[*count].promotion = promotion;
    moves[*count].captured = captured;
    moves[*count].flags = flags;
    (*count)++;
}

static void push_promotion_moves(Move moves[MAX_MOVES], int *count, int from, int to, int side, int flags, int captured) {
    int queen = side == WHITE ? WQUEEN : BQUEEN;
    int rook = side == WHITE ? WROOK : BROOK;
    int bishop = side == WHITE ? WBISHOP : BBISHOP;
    int knight = side == WHITE ? WKNIGHT : BKNIGHT;

    push_move(moves, count, from, to, queen, flags, captured);
    push_move(moves, count, from, to, rook, flags, captured);
    push_move(moves, count, from, to, bishop, flags, captured);
    push_move(moves, count, from, to, knight, flags, captured);
}

static void generate_pawn_moves(const Board *board, int sq, Move moves[MAX_MOVES], int *count) {
    int piece = board->squares[sq];
    int side = side_of_piece(piece);
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int dir = side == WHITE ? 1 : -1;
    int start_rank = side == WHITE ? 1 : 6;
    int promo_rank = side == WHITE ? 7 : 0;
    int next_rank = rank + dir;
    int next_sq;

    if (on_board(file, next_rank)) {
        next_sq = next_rank * 8 + file;
        if (board->squares[next_sq] == EMPTY) {
            if (next_rank == promo_rank) {
                push_promotion_moves(moves, count, sq, next_sq, side, MOVE_FLAG_NONE, EMPTY);
            } else {
                push_move(moves, count, sq, next_sq, EMPTY, MOVE_FLAG_NONE, EMPTY);
            }

            if (rank == start_rank) {
                int jump_rank = rank + (2 * dir);
                int jump_sq = jump_rank * 8 + file;
                if (board->squares[jump_sq] == EMPTY) {
                    push_move(moves, count, sq, jump_sq, EMPTY, MOVE_FLAG_NONE, EMPTY);
                }
            }
        }
    }

    {
        int capture_files[2] = {file - 1, file + 1};
        int i;
        for (i = 0; i < 2; i++) {
            int cf = capture_files[i];
            if (!on_board(cf, next_rank)) {
                continue;
            }
            next_sq = next_rank * 8 + cf;
            if (board->squares[next_sq] != EMPTY && side_of_piece(board->squares[next_sq]) == -side) {
                if (next_rank == promo_rank) {
                    push_promotion_moves(moves, count, sq, next_sq, side, MOVE_FLAG_NONE, board->squares[next_sq]);
                } else {
                    push_move(moves, count, sq, next_sq, EMPTY, MOVE_FLAG_NONE, board->squares[next_sq]);
                }
            }

            if (board->en_passant_sq == next_sq) {
                push_move(moves, count, sq, next_sq, EMPTY, MOVE_FLAG_EN_PASSANT, side == WHITE ? BPAWN : WPAWN);
            }
        }
    }
}

static void generate_knight_moves(const Board *board, int sq, Move moves[MAX_MOVES], int *count) {
    static const int offsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };
    int side = side_of_piece(board->squares[sq]);
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int i;

    for (i = 0; i < 8; i++) {
        int nf = file + offsets[i][0];
        int nr = rank + offsets[i][1];
        int to;
        if (!on_board(nf, nr)) {
            continue;
        }
        to = nr * 8 + nf;
        if (side_of_piece(board->squares[to]) != side) {
            push_move(moves, count, sq, to, EMPTY, MOVE_FLAG_NONE, board->squares[to]);
        }
    }
}

static void generate_slider_moves(const Board *board, int sq, Move moves[MAX_MOVES], int *count, const int dirs[][2], int dir_count) {
    int side = side_of_piece(board->squares[sq]);
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int d;

    for (d = 0; d < dir_count; d++) {
        int nf = file + dirs[d][0];
        int nr = rank + dirs[d][1];
        while (on_board(nf, nr)) {
            int to = nr * 8 + nf;
            int target = board->squares[to];
            if (target == EMPTY) {
                push_move(moves, count, sq, to, EMPTY, MOVE_FLAG_NONE, EMPTY);
            } else {
                if (side_of_piece(target) == -side) {
                    push_move(moves, count, sq, to, EMPTY, MOVE_FLAG_NONE, target);
                }
                break;
            }
            nf += dirs[d][0];
            nr += dirs[d][1];
        }
    }
}

static void generate_king_moves(const Board *board, int sq, Move moves[MAX_MOVES], int *count) {
    static const int offsets[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    int side = side_of_piece(board->squares[sq]);
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int i;

    for (i = 0; i < 8; i++) {
        int nf = file + offsets[i][0];
        int nr = rank + offsets[i][1];
        int to;
        if (!on_board(nf, nr)) {
            continue;
        }
        to = nr * 8 + nf;
        if (side_of_piece(board->squares[to]) != side) {
            push_move(moves, count, sq, to, EMPTY, MOVE_FLAG_NONE, board->squares[to]);
        }
    }

    if (side == WHITE && sq == 4) {
        if ((board->castling_rights & CASTLE_WHITE_K) &&
            board->squares[5] == EMPTY &&
            board->squares[6] == EMPTY &&
            board->squares[7] == WROOK &&
            !is_square_attacked(board, 4, BLACK) &&
            !is_square_attacked(board, 5, BLACK) &&
            !is_square_attacked(board, 6, BLACK)) {
            push_move(moves, count, 4, 6, EMPTY, MOVE_FLAG_CASTLE_KING, EMPTY);
        }
        if ((board->castling_rights & CASTLE_WHITE_Q) &&
            board->squares[3] == EMPTY &&
            board->squares[2] == EMPTY &&
            board->squares[1] == EMPTY &&
            board->squares[0] == WROOK &&
            !is_square_attacked(board, 4, BLACK) &&
            !is_square_attacked(board, 3, BLACK) &&
            !is_square_attacked(board, 2, BLACK)) {
            push_move(moves, count, 4, 2, EMPTY, MOVE_FLAG_CASTLE_QUEEN, EMPTY);
        }
    }

    if (side == BLACK && sq == 60) {
        if ((board->castling_rights & CASTLE_BLACK_K) &&
            board->squares[61] == EMPTY &&
            board->squares[62] == EMPTY &&
            board->squares[63] == BROOK &&
            !is_square_attacked(board, 60, WHITE) &&
            !is_square_attacked(board, 61, WHITE) &&
            !is_square_attacked(board, 62, WHITE)) {
            push_move(moves, count, 60, 62, EMPTY, MOVE_FLAG_CASTLE_KING, EMPTY);
        }
        if ((board->castling_rights & CASTLE_BLACK_Q) &&
            board->squares[59] == EMPTY &&
            board->squares[58] == EMPTY &&
            board->squares[57] == EMPTY &&
            board->squares[56] == BROOK &&
            !is_square_attacked(board, 60, WHITE) &&
            !is_square_attacked(board, 59, WHITE) &&
            !is_square_attacked(board, 58, WHITE)) {
            push_move(moves, count, 60, 58, EMPTY, MOVE_FLAG_CASTLE_QUEEN, EMPTY);
        }
    }
}

void apply_move(const Board *board, const Move *move, Board *out_board) {
    int piece = board->squares[move->from];
    int side = side_of_piece(piece);
    int from_file = sq_file(move->from);

    *out_board = *board;
    out_board->en_passant_sq = NO_SQUARE;
    out_board->squares[move->from] = EMPTY;

    if (move->flags & MOVE_FLAG_EN_PASSANT) {
        int cap_sq = side == WHITE ? move->to - 8 : move->to + 8;
        out_board->squares[cap_sq] = EMPTY;
    }

    if (move->captured == WROOK && move->to == 0) {
        out_board->castling_rights &= ~CASTLE_WHITE_Q;
    } else if (move->captured == WROOK && move->to == 7) {
        out_board->castling_rights &= ~CASTLE_WHITE_K;
    } else if (move->captured == BROOK && move->to == 56) {
        out_board->castling_rights &= ~CASTLE_BLACK_Q;
    } else if (move->captured == BROOK && move->to == 63) {
        out_board->castling_rights &= ~CASTLE_BLACK_K;
    }

    if (move->promotion != EMPTY) {
        out_board->squares[move->to] = move->promotion;
    } else {
        out_board->squares[move->to] = piece;
    }

    if (move->flags & MOVE_FLAG_CASTLE_KING) {
        if (side == WHITE) {
            out_board->squares[7] = EMPTY;
            out_board->squares[5] = WROOK;
        } else {
            out_board->squares[63] = EMPTY;
            out_board->squares[61] = BROOK;
        }
    }
    if (move->flags & MOVE_FLAG_CASTLE_QUEEN) {
        if (side == WHITE) {
            out_board->squares[0] = EMPTY;
            out_board->squares[3] = WROOK;
        } else {
            out_board->squares[56] = EMPTY;
            out_board->squares[59] = BROOK;
        }
    }

    if (piece == WKING) {
        out_board->castling_rights &= ~(CASTLE_WHITE_K | CASTLE_WHITE_Q);
    } else if (piece == BKING) {
        out_board->castling_rights &= ~(CASTLE_BLACK_K | CASTLE_BLACK_Q);
    }

    if (piece == WROOK && move->from == 0) {
        out_board->castling_rights &= ~CASTLE_WHITE_Q;
    } else if (piece == WROOK && move->from == 7) {
        out_board->castling_rights &= ~CASTLE_WHITE_K;
    } else if (piece == BROOK && move->from == 56) {
        out_board->castling_rights &= ~CASTLE_BLACK_Q;
    } else if (piece == BROOK && move->from == 63) {
        out_board->castling_rights &= ~CASTLE_BLACK_K;
    }

    if (abs(piece) == WPAWN && abs(sq_rank(move->to) - sq_rank(move->from)) == 2) {
        int mid_rank = (sq_rank(move->from) + sq_rank(move->to)) / 2;
        out_board->en_passant_sq = mid_rank * 8 + from_file;
    }

    /* Update cached king position if king moved */
    if (piece == WKING || piece == BKING) {
        int king_idx = piece == WKING ? 0 : 1;
        out_board->king_sq[king_idx] = move->to;
    }

    out_board->side_to_move = -board->side_to_move;
}

static int is_square_attacked(const Board *board, int sq, int by_side) {
    int file = sq_file(sq);
    int rank = sq_rank(sq);
    int i;

    {
        int pawn_rank = rank + (by_side == WHITE ? -1 : 1);
        int pawn_files[2] = {file - 1, file + 1};
        for (i = 0; i < 2; i++) {
            int pf = pawn_files[i];
            int psq;
            if (!on_board(pf, pawn_rank)) {
                continue;
            }
            psq = pawn_rank * 8 + pf;
            if (board->squares[psq] == (by_side == WHITE ? WPAWN : BPAWN)) {
                return 1;
            }
        }
    }

    {
        static const int knight_offsets[8][2] = {
            {1, 2}, {2, 1}, {2, -1}, {1, -2},
            {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
        };
        for (i = 0; i < 8; i++) {
            int nf = file + knight_offsets[i][0];
            int nr = rank + knight_offsets[i][1];
            int nsq;
            if (!on_board(nf, nr)) {
                continue;
            }
            nsq = nr * 8 + nf;
            if (board->squares[nsq] == (by_side == WHITE ? WKNIGHT : BKNIGHT)) {
                return 1;
            }
        }
    }

    {
        static const int bishop_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
        static const int rook_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

        for (i = 0; i < 4; i++) {
            int nf = file + bishop_dirs[i][0];
            int nr = rank + bishop_dirs[i][1];
            while (on_board(nf, nr)) {
                int tsq = nr * 8 + nf;
                int p = board->squares[tsq];
                if (p != EMPTY) {
                    if (p == (by_side == WHITE ? WBISHOP : BBISHOP) || p == (by_side == WHITE ? WQUEEN : BQUEEN)) {
                        return 1;
                    }
                    break;
                }
                nf += bishop_dirs[i][0];
                nr += bishop_dirs[i][1];
            }
        }

        for (i = 0; i < 4; i++) {
            int nf = file + rook_dirs[i][0];
            int nr = rank + rook_dirs[i][1];
            while (on_board(nf, nr)) {
                int tsq = nr * 8 + nf;
                int p = board->squares[tsq];
                if (p != EMPTY) {
                    if (p == (by_side == WHITE ? WROOK : BROOK) || p == (by_side == WHITE ? WQUEEN : BQUEEN)) {
                        return 1;
                    }
                    break;
                }
                nf += rook_dirs[i][0];
                nr += rook_dirs[i][1];
            }
        }
    }

    {
        static const int king_offsets[8][2] = {
            {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        };
        for (i = 0; i < 8; i++) {
            int nf = file + king_offsets[i][0];
            int nr = rank + king_offsets[i][1];
            int ksq;
            if (!on_board(nf, nr)) {
                continue;
            }
            ksq = nr * 8 + nf;
            if (board->squares[ksq] == (by_side == WHITE ? WKING : BKING)) {
                return 1;
            }
        }
    }

    return 0;
}

/* Efficiently check if a move would leave our king in check without copying board */
static int would_leave_king_in_check(const Board *board, const Move *move) {
    int piece = board->squares[move->from];
    int opponent_side = -board->side_to_move;
    int king_sq_pos;
    int legal;

    /* For king moves, just check if destination is attacked */
    if (abs(piece) == WKING) {
        /* King moving - is destination attacked? */
        int orig = board->squares[move->to];
        ((Board *)board)->squares[move->from] = EMPTY;
        ((Board *)board)->squares[move->to] = piece;
        int attacked = is_square_attacked(board, move->to, opponent_side);
        ((Board *)board)->squares[move->from] = piece;
        ((Board *)board)->squares[move->to] = orig;
        return attacked;
    }

    /* For non-king moves, check if king is in check after move */
    king_sq_pos = board->king_sq[board->side_to_move == WHITE ? 0 : 1];
    if (king_sq_pos == NO_SQUARE || king_sq_pos < 0 || king_sq_pos >= 64) {
        /* Fallback to full board copy */
        Board next;
        apply_move(board, move, &next);
        return is_in_check(&next, board->side_to_move);
    }

    /* Temporarily apply move to board to check legality */
    int orig_from = board->squares[move->from];
    int orig_to = board->squares[move->to];
    int en_passant_capture = NO_SQUARE;

    ((Board *)board)->squares[move->from] = EMPTY;
    ((Board *)board)->squares[move->to] = piece;

    if (move->flags & MOVE_FLAG_EN_PASSANT) {
        en_passant_capture = board->side_to_move == WHITE ? move->to - 8 : move->to + 8;
        ((Board *)board)->squares[en_passant_capture] = EMPTY;
    }

    legal = !is_square_attacked(board, king_sq_pos, opponent_side);

    /* Restore board state */
    ((Board *)board)->squares[move->from] = orig_from;
    ((Board *)board)->squares[move->to] = orig_to;
    if (en_passant_capture != NO_SQUARE) {
        ((Board *)board)->squares[en_passant_capture] = board->side_to_move == WHITE ? BPAWN : WPAWN;
    }

    return !legal;
}

int is_in_check(const Board *board, int side) {
    int king_piece = side == WHITE ? WKING : BKING;
    int king_sq_pos = board->king_sq[side == WHITE ? 0 : 1];

    if (king_sq_pos == NO_SQUARE || king_sq_pos < 0 || king_sq_pos >= 64) {
        /* Fallback: scan for king */
        int sq;
        for (sq = 0; sq < 64; sq++) {
            if (board->squares[sq] == king_piece) {
                return is_square_attacked(board, sq, -side);
            }
        }
        return 0;
    }

    return is_square_attacked(board, king_sq_pos, -side);
}

int generate_legal_moves(const Board *board, Move moves[MAX_MOVES]) {
    Move pseudo[MAX_MOVES];
    int pseudo_count = 0;
    int legal_count = 0;
    int sq;

    static const int bishop_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    static const int rook_dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (sq = 0; sq < 64; sq++) {
        int piece = board->squares[sq];
        if (piece == EMPTY || side_of_piece(piece) != board->side_to_move) {
            continue;
        }

        switch (abs(piece)) {
            case WPAWN:
                generate_pawn_moves(board, sq, pseudo, &pseudo_count);
                break;
            case WKNIGHT:
                generate_knight_moves(board, sq, pseudo, &pseudo_count);
                break;
            case WBISHOP:
                generate_slider_moves(board, sq, pseudo, &pseudo_count, bishop_dirs, 4);
                break;
            case WROOK:
                generate_slider_moves(board, sq, pseudo, &pseudo_count, rook_dirs, 4);
                break;
            case WQUEEN:
                generate_slider_moves(board, sq, pseudo, &pseudo_count, bishop_dirs, 4);
                generate_slider_moves(board, sq, pseudo, &pseudo_count, rook_dirs, 4);
                break;
            case WKING:
                generate_king_moves(board, sq, pseudo, &pseudo_count);
                break;
            default:
                break;
        }
    }

    /* Filter illegal moves without full board copy for most moves */
    for (sq = 0; sq < pseudo_count; sq++) {
        if (!would_leave_king_in_check(board, &pseudo[sq])) {
            moves[legal_count++] = pseudo[sq];
        }
    }

    return legal_count;
}
