#include "chess.h"

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

int choose_best_move(const Board *board, int depth, Move *best_move) {
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
