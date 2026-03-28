#include "chess.h"

#include <stdio.h>

static int negamax(const Board *board, int depth, int alpha, int beta, int ply) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(board, moves);
    int i;

    if (depth == 0) {
        return evaluate_board(board) * board->side_to_move;
    }

    if (count == 0) {
        if (is_in_check(board, board->side_to_move)) {
            return -CHECKMATE_SCORE + ply;
        }
        return 0;
    }

    for (i = 0; i < count; i++) {
        Board next;
        int score;

        apply_move(board, &moves[i], &next);
        score = -negamax(&next, depth - 1, -beta, -alpha, ply + 1);

        if (score > alpha) {
            alpha = score;
        }
        if (alpha >= beta) {
            break;
        }
    }

    return alpha;
}

static int choose_best_move_at_depth(const Board *board, int depth, Move *best_move) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(board, moves);
    int best_score = -CHECKMATE_SCORE;
    int alpha = -CHECKMATE_SCORE;
    int beta = CHECKMATE_SCORE;
    int i;

    if (count == 0) {
        return 0;
    }

    *best_move = moves[0];

    for (i = 0; i < count; i++) {
        Board next;
        int score;

        apply_move(board, &moves[i], &next);
        score = -negamax(&next, depth - 1, -beta, -alpha, 1);

        if (score > best_score) {
            best_score = score;
            *best_move = moves[i];
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return best_score;
}

int choose_best_move_with_debug(const Board *board, int depth, Move *best_move, FILE *debug_log) {
    int d;
    int score = 0;

    if (!best_move) {
        return 0;
    }

    if (depth < 1) {
        depth = 1;
    }

    for (d = 1; d <= depth; d++) {
        Move depth_best_move;
        char uci[6];

        score = choose_best_move_at_depth(board, d, &depth_best_move);
        if (d == depth) {
            *best_move = depth_best_move;
        }

        if (debug_log) {
            move_to_uci(&depth_best_move, uci);
            fprintf(debug_log, "depth=%d best=%s score=%d\n", d, uci, score);
            fflush(debug_log);
        }
    }

    return score;
}

int choose_best_move(const Board *board, int depth, Move *best_move) {
    return choose_best_move_with_debug(board, depth, best_move, NULL);
}
