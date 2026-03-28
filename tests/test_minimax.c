#include "chess.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_start_position_has_moves(void) {
    Board board;
    Move moves[MAX_MOVES];
    int count;

    board_set_startpos(&board);
    count = generate_legal_moves(&board, moves);
    assert(count == 20);
}

static void test_checkmate_position_has_no_moves(void) {
    Board board;
    Move moves[MAX_MOVES];
    int count;

    /* Black king on h8 checkmated by white queen g7 and king f6. */
    assert(board_from_fen(&board, "7k/6Q1/5K2/8/8/8/8/8 b") == 1);

    count = generate_legal_moves(&board, moves);
    assert(count == 0);
    assert(is_in_check(&board, BLACK) == 1);
}

static void test_castling_moves_generated(void) {
    Board board;
    Move moves[MAX_MOVES];
    int count;
    int i;
    int has_king_side = 0;
    int has_queen_side = 0;

    assert(board_from_fen(&board, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1") == 1);
    count = generate_legal_moves(&board, moves);

    for (i = 0; i < count; i++) {
        char uci[6];
        move_to_uci(&moves[i], uci);
        if (strcmp(uci, "e1g1") == 0) {
            has_king_side = 1;
        }
        if (strcmp(uci, "e1c1") == 0) {
            has_queen_side = 1;
        }
    }

    assert(has_king_side == 1);
    assert(has_queen_side == 1);
}

static void test_en_passant_is_legal_and_applied(void) {
    Board board;
    Move ep_move;
    Board next;

    assert(board_from_fen(&board, "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1") == 1);
    assert(parse_uci_move(&board, "e5d6", &ep_move) == 1);

    apply_move(&board, &ep_move, &next);
    assert(next.squares[43] == WPAWN);
    assert(next.squares[35] == EMPTY);
}

int main(void) {
    test_start_position_has_moves();
    test_checkmate_position_has_no_moves();
    test_castling_moves_generated();
    test_en_passant_is_legal_and_applied();

    printf("All tests passed.\n");
    return 0;
}
