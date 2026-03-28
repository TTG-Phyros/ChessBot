#include "chess.h"

#include <stdio.h>
#include <string.h>

static void strip_newline(char *s) {
    size_t i;
    for (i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\n' || s[i] == '\r') {
            s[i] = '\0';
            return;
        }
    }
}

int main(int argc, char **argv) {
    Board board;
    int depth = 4;
    char input[64];

    if (argc > 1) {
        if (sscanf(argv[1], "%d", &depth) != 1 || depth < 1 || depth > 8) {
            fprintf(stderr, "Usage: %s [depth 1-8]\n", argv[0]);
            return 1;
        }
    }

    board_set_startpos(&board);

    printf("MiniMax Chess Bot (C)\n");
    printf("You are white. Enter UCI moves like e2e4. Type 'quit' to stop.\n");
    printf("Engine search depth: %d\n", depth);

    while (1) {
        Move user_move;
        Move engine_move;
        Move legal_moves[MAX_MOVES];
        char uci[6];
        int legal_count;
        int score;

        board_print(&board);
        legal_count = generate_legal_moves(&board, legal_moves);
        if (legal_count == 0) {
            if (is_in_check(&board, board.side_to_move)) {
                printf("Checkmate. %s wins.\n", board.side_to_move == WHITE ? "Engine" : "You");
            } else {
                printf("Stalemate.\n");
            }
            break;
        }

        if (board.side_to_move == WHITE) {
            printf("Your move> ");
            if (!fgets(input, sizeof(input), stdin)) {
                break;
            }
            strip_newline(input);

            if (strcmp(input, "quit") == 0) {
                break;
            }

            if (!parse_uci_move(&board, input, &user_move)) {
                printf("Invalid or illegal move: %s\n", input);
                continue;
            }

            apply_move(&board, &user_move, &board);
            continue;
        }

        score = choose_best_move(&board, depth, &engine_move);
        move_to_uci(&engine_move, uci);
        printf("Engine plays: %s (score=%d)\n", uci, score);
        apply_move(&board, &engine_move, &board);
    }

    return 0;
}
