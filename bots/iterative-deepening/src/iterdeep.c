#include "chess.h"

#include <stdio.h>

/* Iterative Deepening Deepening Search
 * This bot uses iterative deepening with alpha-beta pruning.
 * It incrementally searches to greater depths and returns the best move found
 * at the target depth. This approach provides better move ordering from shallower
 * searches, which improves alpha-beta pruning efficiency.
 */

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

static int same_move(const Move *a, const Move *b) {
    if (!a || !b) {
        return 0;
    }
    return (
        a->from == b->from &&
        a->to == b->to &&
        a->promotion == b->promotion &&
        a->flags == b->flags
    );
}

static void bring_move_to_front(Move moves[MAX_MOVES], int count, const Move *target) {
    int i;

    if (!target || count <= 1) {
        return;
    }

    for (i = 0; i < count; i++) {
        if (same_move(&moves[i], target)) {
            Move tmp = moves[0];
            moves[0] = moves[i];
            moves[i] = tmp;
            return;
        }
    }
}

int choose_best_move_with_debug(const Board *board, int max_depth, Move *best_move, FILE *debug_log) {
    Move moves[MAX_MOVES];
    int count = generate_legal_moves(board, moves);
    int best_score = -CHECKMATE_SCORE;
    int current_depth;
    int i;
    Move pv_move;
    int has_pv = 0;

    if (count == 0) {
        return 0;
    }

    *best_move = moves[0];
    pv_move = moves[0];

    /* Iterative deepening: search progressively deeper */
    for (current_depth = 1; current_depth <= max_depth; current_depth++) {
        int depth_best_score = -CHECKMATE_SCORE;
        Move depth_best_move = moves[0];
        int alpha = -CHECKMATE_SCORE;
        int beta = CHECKMATE_SCORE;

        if (has_pv) {
            bring_move_to_front(moves, count, &pv_move);
        }

        for (i = 0; i < count; i++) {
            Board next;
            int score;

            apply_move(board, &moves[i], &next);
            score = -negamax(&next, current_depth - 1, -beta, -alpha, 1);

            if (score > depth_best_score) {
                depth_best_score = score;
                depth_best_move = moves[i];
            }
            if (score > alpha) {
                alpha = score;
            }
        }

        best_score = depth_best_score;
        *best_move = depth_best_move;
        pv_move = depth_best_move;
        has_pv = 1;

        if (debug_log) {
            char uci[6];
            move_to_uci(&depth_best_move, uci);
            fprintf(debug_log, "depth=%d best=%s score=%d\n", current_depth, uci, depth_best_score);
            fflush(debug_log);
        }
    }

    return best_score;
}

int choose_best_move(const Board *board, int max_depth, Move *best_move) {
    return choose_best_move_with_debug(board, max_depth, best_move, NULL);
}
