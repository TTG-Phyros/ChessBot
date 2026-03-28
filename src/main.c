#include "chess.h"

#include <stdio.h>
#include <stdlib.h>
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

static int parse_depth_value(const char *s, int *out_depth) {
    int parsed;
    if (!s || !out_depth) {
        return 0;
    }
    if (sscanf(s, "%d", &parsed) != 1 || parsed < 1 || parsed > 8) {
        return 0;
    }
    *out_depth = parsed;
    return 1;
}

static void print_json_escaped(const char *s) {
    size_t i;
    for (i = 0; s && s[i] != '\0'; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            putchar('\\');
        }
        putchar((unsigned char)s[i]);
    }
}

static void emit_state_json(const Board *board, const char *engine_move, int engine_score, int depth) {
    Move moves[MAX_MOVES];
    int move_count = generate_legal_moves(board, moves);
    int in_check = is_in_check(board, board->side_to_move);
    int i;

    printf("{\"squares\":[");
    for (i = 0; i < 64; i++) {
        if (i > 0) {
            putchar(',');
        }
        printf("%d", board->squares[i]);
    }
    printf("],\"side_to_move\":%d,\"castling_rights\":%d,\"en_passant_sq\":%d,",
           board->side_to_move,
           board->castling_rights,
           board->en_passant_sq);

    printf("\"legal_moves\":[");
    for (i = 0; i < move_count; i++) {
        char uci[6];
        move_to_uci(&moves[i], uci);
        if (i > 0) {
            putchar(',');
        }
        printf("{\"uci\":\"");
        print_json_escaped(uci);
        printf("\",\"from\":%d,\"to\":%d}", moves[i].from, moves[i].to);
    }
    printf("],\"move_count\":%d,\"in_check\":%s",
           move_count,
           in_check ? "true" : "false");

    if (engine_move) {
        printf(",\"engine_move\":\"");
        print_json_escaped(engine_move);
        printf("\",\"engine_score\":%d,\"engine_depth\":%d", engine_score, depth);
    }

    printf("}\n");
    fflush(stdout);
}

static int run_protocol_mode(int depth) {
    Board board;
    char input[128];

    board_set_startpos(&board);

    while (fgets(input, sizeof(input), stdin)) {
        Move user_move;
        Board next_board;
        char *cmd;
        char *arg;

        strip_newline(input);
        cmd = strtok(input, " ");
        arg = strtok(NULL, " ");

        if (!cmd) {
            printf("{\"error\":\"Empty command\"}\n");
            fflush(stdout);
            continue;
        }

        if (strcmp(cmd, "quit") == 0) {
            break;
        }

        if (strcmp(cmd, "board") == 0) {
            emit_state_json(&board, NULL, 0, depth);
            continue;
        }

        if (strcmp(cmd, "reset") == 0) {
            board_set_startpos(&board);
            emit_state_json(&board, NULL, 0, depth);
            continue;
        }

        if (strcmp(cmd, "move") == 0) {
            Move legal_moves[MAX_MOVES];
            int legal_count;

            if (!arg || !parse_uci_move(&board, arg, &user_move)) {
                printf("{\"error\":\"Illegal move\"}\n");
                fflush(stdout);
                continue;
            }

            apply_move(&board, &user_move, &next_board);
            board = next_board;

            legal_count = generate_legal_moves(&board, legal_moves);
            if (legal_count == 0) {
                emit_state_json(&board, "", 0, depth);
                continue;
            }

            {
                Move engine_move;
                int score = choose_best_move(&board, depth, &engine_move);
                char engine_uci[6];

                move_to_uci(&engine_move, engine_uci);
                apply_move(&board, &engine_move, &next_board);
                board = next_board;
                emit_state_json(&board, engine_uci, score, depth);
            }
            continue;
        }

        printf("{\"error\":\"Unknown command\"}\n");
        fflush(stdout);
    }

    return 0;
}

int main(int argc, char **argv) {
    Board board;
    int depth = 4;
    const char *env_depth;
    char input[64];
    int protocol_mode = 0;
    int argi = 1;

    env_depth = getenv("BOT_DEPTH");
    if (env_depth && !parse_depth_value(env_depth, &depth)) {
        fprintf(stderr, "Ignoring invalid BOT_DEPTH='%s' (expected 1-8)\n", env_depth);
        depth = 4;
    }

    if (argc > 1 && strcmp(argv[1], "--protocol") == 0) {
        protocol_mode = 1;
        argi = 2;
    }

    if (argc > argi) {
        if (!parse_depth_value(argv[argi], &depth)) {
            if (protocol_mode) {
                fprintf(stderr, "Usage: %s --protocol [depth 1-8]\n", argv[0]);
            } else {
                fprintf(stderr, "Usage: %s [depth 1-8]\n", argv[0]);
            }
            return 1;
        }
    }

    if (protocol_mode) {
        return run_protocol_mode(depth);
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
