#include "chess.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_GAME_PLIES 512

typedef struct {
    char moves[MAX_GAME_PLIES][24];
    int count;
    int pgn_written;
} GameRecorder;

static void format_timestamp(char *out, size_t out_size) {
    time_t now;
    struct tm *tm_now;

    if (!out || out_size == 0) {
        return;
    }

    now = time(NULL);
    tm_now = localtime(&now);
    if (!tm_now || strftime(out, out_size, "%Y-%m-%d %H:%M:%S", tm_now) == 0) {
        snprintf(out, out_size, "unknown-time");
    }
}

static void log_search_start(
    FILE *debug_log,
    int request_id,
    const char *mode,
    const char *player_move,
    int depth
) {
    char ts[32];

    if (!debug_log) {
        return;
    }

    format_timestamp(ts, sizeof(ts));
    fprintf(debug_log, "[%s] SEARCH_START id=%d mode=%s player_move=%s max_depth=%d\n",
            ts,
            request_id,
            mode ? mode : "unknown",
            player_move ? player_move : "(none)",
            depth);
    fflush(debug_log);
}

static void log_search_end(
    FILE *debug_log,
    int request_id,
    const char *engine_move,
    int score,
    long long think_ms
) {
    char ts[32];

    if (!debug_log) {
        return;
    }

    format_timestamp(ts, sizeof(ts));
    fprintf(debug_log, "[%s] SEARCH_END id=%d engine_move=%s score=%d think_ms=%lld\n",
            ts,
            request_id,
            engine_move ? engine_move : "(none)",
            score,
            think_ms);
    fflush(debug_log);
}

static long long now_ms(void) {
    struct timespec ts;

    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) {
        return 0;
    }

    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static void write_debug_banner(
    FILE *debug_log,
    const char *algorithm,
    const char *eval_profile,
    int depth,
    int protocol_mode,
    const char *log_path
) {
    char ts[32];

    if (!debug_log) {
        return;
    }

    format_timestamp(ts, sizeof(ts));
    fprintf(debug_log,
            "\n============================================================\n"
            "CHESSBOT DEBUG SESSION\n"
            "started_at: %s\n"
            "algorithm: %s\n"
            "evaluation: %s\n"
            "parameters: depth=%d mode=%s\n"
            "log_file: %s\n"
            "============================================================\n",
            ts,
            algorithm ? algorithm : "unknown",
            eval_profile ? eval_profile : "unknown",
            depth,
            protocol_mode ? "protocol" : "cli",
            log_path ? log_path : "unknown");
    fflush(debug_log);
}

static void reset_game_recorder(GameRecorder *recorder) {
    if (!recorder) {
        return;
    }
    recorder->count = 0;
    recorder->pgn_written = 0;
}

static int is_same_move(const Move *a, const Move *b) {
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

static char square_file(int sq) {
    return (char)('a' + (sq % 8));
}

static char square_rank(int sq) {
    return (char)('1' + (sq / 8));
}

static char piece_to_san_letter(int piece) {
    switch (piece) {
        case WKNIGHT:
        case BKNIGHT:
            return 'N';
        case WBISHOP:
        case BBISHOP:
            return 'B';
        case WROOK:
        case BROOK:
            return 'R';
        case WQUEEN:
        case BQUEEN:
            return 'Q';
        case WKING:
        case BKING:
            return 'K';
        default:
            return '\0';
    }
}

static char promotion_to_san_letter(int piece) {
    switch (piece) {
        case WKNIGHT:
        case BKNIGHT:
            return 'N';
        case WBISHOP:
        case BBISHOP:
            return 'B';
        case WROOK:
        case BROOK:
            return 'R';
        case WQUEEN:
        case BQUEEN:
            return 'Q';
        default:
            return 'Q';
    }
}

static void build_san_move(const Board *board_before, const Move *move, char *out_san, size_t out_san_size) {
    int from_piece;
    int is_pawn;
    int is_capture;
    char san[32];
    int pos = 0;

    if (!board_before || !move || !out_san || out_san_size == 0) {
        return;
    }

    san[0] = '\0';
    from_piece = board_before->squares[move->from];
    is_pawn = (from_piece == WPAWN || from_piece == BPAWN);
    is_capture = (move->captured != EMPTY) || ((move->flags & MOVE_FLAG_EN_PASSANT) != 0);

    if (move->flags & MOVE_FLAG_CASTLE_KING) {
        snprintf(san, sizeof(san), "O-O");
    } else if (move->flags & MOVE_FLAG_CASTLE_QUEEN) {
        snprintf(san, sizeof(san), "O-O-O");
    } else {
        if (!is_pawn) {
            Move legal_moves[MAX_MOVES];
            int legal_count = generate_legal_moves(board_before, legal_moves);
            int i;
            int ambiguous = 0;
            int same_file = 0;
            int same_rank = 0;
            char piece_letter = piece_to_san_letter(from_piece);

            san[pos++] = piece_letter;

            for (i = 0; i < legal_count; i++) {
                int other_piece = board_before->squares[legal_moves[i].from];
                if (!is_same_move(&legal_moves[i], move) &&
                    legal_moves[i].to == move->to &&
                    other_piece == from_piece) {
                    ambiguous = 1;
                    if (square_file(legal_moves[i].from) == square_file(move->from)) {
                        same_file = 1;
                    }
                    if (square_rank(legal_moves[i].from) == square_rank(move->from)) {
                        same_rank = 1;
                    }
                }
            }

            if (ambiguous) {
                if (!same_file) {
                    san[pos++] = square_file(move->from);
                } else if (!same_rank) {
                    san[pos++] = square_rank(move->from);
                } else {
                    san[pos++] = square_file(move->from);
                    san[pos++] = square_rank(move->from);
                }
            }
        } else if (is_capture) {
            san[pos++] = square_file(move->from);
        }

        if (is_capture) {
            san[pos++] = 'x';
        }

        san[pos++] = square_file(move->to);
        san[pos++] = square_rank(move->to);

        if (move->promotion != EMPTY) {
            san[pos++] = '=';
            san[pos++] = promotion_to_san_letter(move->promotion);
        }

        san[pos] = '\0';
    }

    {
        Board board_after;
        Move legal_after[MAX_MOVES];
        int legal_after_count;
        int in_check_after;
        size_t len;

        apply_move(board_before, move, &board_after);
        legal_after_count = generate_legal_moves(&board_after, legal_after);
        in_check_after = is_in_check(&board_after, board_after.side_to_move);
        len = strlen(san);

        if (in_check_after) {
            if (legal_after_count == 0) {
                san[len++] = '#';
            } else {
                san[len++] = '+';
            }
            san[len] = '\0';
        }
    }

    snprintf(out_san, out_san_size, "%s", san);
}

static void record_move_san(GameRecorder *recorder, const char *san) {
    if (!recorder || !san || san[0] == '\0') {
        return;
    }
    if (recorder->count >= MAX_GAME_PLIES) {
        return;
    }

    snprintf(recorder->moves[recorder->count], sizeof(recorder->moves[recorder->count]), "%s", san);
    recorder->count++;
}

static void write_pgn_moves(FILE *debug_log, const GameRecorder *recorder, const char *result) {
    int i;
    int current_line_len = 0;

    if (!debug_log || !recorder) {
        return;
    }

    for (i = 0; i < recorder->count; i += 2) {
        char token[32];
        int token_len;

        if (i + 1 < recorder->count) {
            snprintf(token, sizeof(token), "%d. %s %s", (i / 2) + 1, recorder->moves[i], recorder->moves[i + 1]);
        } else {
            snprintf(token, sizeof(token), "%d. %s", (i / 2) + 1, recorder->moves[i]);
        }

        token_len = (int)strlen(token);
        if (current_line_len > 0) {
            if (current_line_len + 1 + token_len > 95) {
                fputc('\n', debug_log);
                current_line_len = 0;
            } else {
                fputc(' ', debug_log);
                current_line_len++;
            }
        }

        fputs(token, debug_log);
        current_line_len += token_len;
    }

    if (result && result[0] != '\0') {
        if (current_line_len + 1 + (int)strlen(result) > 95) {
            fputc('\n', debug_log);
        } else {
            fputc(' ', debug_log);
        }
        fputs(result, debug_log);
    }
    fputc('\n', debug_log);
}

static void append_game_pgn_if_finished(
    FILE *debug_log,
    GameRecorder *recorder,
    const Board *board,
    const char *algorithm,
    const char *eval_profile,
    int depth,
    const char *mode
) {
    Move legal_moves[MAX_MOVES];
    int legal_count;
    int in_check;
    const char *result;
    const char *termination;
    char date_str[16];
    char end_time[32];
    time_t now;
    struct tm *tm_now;

    if (!debug_log || !recorder || !board || recorder->pgn_written) {
        return;
    }

    legal_count = generate_legal_moves(board, legal_moves);
    if (legal_count != 0) {
        return;
    }

    in_check = is_in_check(board, board->side_to_move);
    if (in_check) {
        result = (board->side_to_move == WHITE) ? "0-1" : "1-0";
        termination = "checkmate";
    } else {
        result = "1/2-1/2";
        termination = "stalemate";
    }

    now = time(NULL);
    tm_now = localtime(&now);
    if (!tm_now || strftime(date_str, sizeof(date_str), "%Y.%m.%d", tm_now) == 0) {
        snprintf(date_str, sizeof(date_str), "unknown");
    }
    if (!tm_now || strftime(end_time, sizeof(end_time), "%H:%M:%S", tm_now) == 0) {
        snprintf(end_time, sizeof(end_time), "unknown");
    }

    fprintf(debug_log,
            "\n[Event \"ChessBot Match\"]\n"
            "[Site \"Local Docker\"]\n"
            "[Date \"%s\"]\n"
            "[Round \"?\"]\n"
            "[White \"Human\"]\n"
            "[Black \"ChessBot\"]\n"
            "[Result \"%s\"]\n"
            "[Termination \"%s\"]\n"
            "[Algorithm \"%s\"]\n"
            "[Eval \"%s\"]\n"
            "[Depth \"%d\"]\n"
            "[Mode \"%s\"]\n"
            "[EndTime \"%s\"]\n\n",
            date_str,
            result,
            termination,
            algorithm ? algorithm : "unknown",
            eval_profile ? eval_profile : "unknown",
            depth,
            mode ? mode : "unknown",
            end_time);

    write_pgn_moves(debug_log, recorder, result);
    fflush(debug_log);
    recorder->pgn_written = 1;
}

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

static int parse_debug_enabled(const char *s) {
    if (!s || s[0] == '\0') {
        return 0;
    }
    return strcmp(s, "0") != 0;
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

static int run_protocol_mode(int depth, FILE *debug_log) {
    Board board;
    char input[128];
    int request_id = 0;
    GameRecorder game;

    board_set_startpos(&board);
    reset_game_recorder(&game);

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
            reset_game_recorder(&game);
            emit_state_json(&board, NULL, 0, depth);
            continue;
        }

        if (strcmp(cmd, "move") == 0) {
            Move legal_moves[MAX_MOVES];
            int legal_count;
            char player_san[24];

            if (!arg || !parse_uci_move(&board, arg, &user_move)) {
                printf("{\"error\":\"Illegal move\"}\n");
                fflush(stdout);
                continue;
            }

            build_san_move(&board, &user_move, player_san, sizeof(player_san));
            apply_move(&board, &user_move, &next_board);
            board = next_board;
            record_move_san(&game, player_san);

            legal_count = generate_legal_moves(&board, legal_moves);
            if (legal_count == 0) {
                append_game_pgn_if_finished(
                    debug_log,
                    &game,
                    &board,
                    "minimax",
                    get_evaluation_profile(),
                    depth,
                    "protocol"
                );
                emit_state_json(&board, "", 0, depth);
                continue;
            }

            {
                Move engine_move;
                int current_request_id;
                int score;
                char engine_uci[6];
                char engine_san[24];
                long long started_ms;
                long long ended_ms;
                long long think_ms;

                request_id++;
                current_request_id = request_id;
                log_search_start(debug_log, current_request_id, "protocol", arg, depth);
                started_ms = now_ms();
                score = choose_best_move_with_debug(&board, depth, &engine_move, debug_log);
                ended_ms = now_ms();
                think_ms = (ended_ms >= started_ms) ? (ended_ms - started_ms) : 0;

                move_to_uci(&engine_move, engine_uci);
                build_san_move(&board, &engine_move, engine_san, sizeof(engine_san));
                log_search_end(debug_log, current_request_id, engine_uci, score, think_ms);
                apply_move(&board, &engine_move, &next_board);
                board = next_board;
                record_move_san(&game, engine_san);
                append_game_pgn_if_finished(
                    debug_log,
                    &game,
                    &board,
                    "minimax",
                    get_evaluation_profile(),
                    depth,
                    "protocol"
                );
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
    const char *env_eval;
    const char *env_debug;
    const char *env_debug_log;
    int debug_enabled = 0;
    const char *debug_log_path = "bot-search.log";
    FILE *debug_log = NULL;
    char input[64];
    int protocol_mode = 0;
    int argi = 1;
    int request_id = 0;
    char last_user_uci[8] = "";
    GameRecorder game;

    env_depth = getenv("BOT_DEPTH");
    if (env_depth && !parse_depth_value(env_depth, &depth)) {
        fprintf(stderr, "Ignoring invalid BOT_DEPTH='%s' (expected 1-8)\n", env_depth);
        depth = 4;
    }

    env_eval = getenv("BOT_EVAL");
    set_evaluation_profile(env_eval);

    env_debug = getenv("BOT_DEBUG");
    debug_enabled = parse_debug_enabled(env_debug);
    env_debug_log = getenv("BOT_DEBUG_LOG");
    if (env_debug_log && env_debug_log[0] != '\0') {
        debug_log_path = env_debug_log;
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

    if (debug_enabled) {
        debug_log = fopen(debug_log_path, "a");
        if (!debug_log) {
            fprintf(stderr, "Warning: failed to open debug log file '%s'\n", debug_log_path);
        } else {
            write_debug_banner(debug_log, "minimax", get_evaluation_profile(), depth, protocol_mode, debug_log_path);
        }
    }

    if (protocol_mode) {
        int ret = run_protocol_mode(depth, debug_log);
        if (debug_log) {
            fclose(debug_log);
        }
        return ret;
    }

    board_set_startpos(&board);
    reset_game_recorder(&game);

    printf("MiniMax Chess Bot (C)\n");
    printf("You are white. Enter UCI moves like e2e4. Type 'quit' to stop.\n");
    printf("Engine search depth: %d\n", depth);
    printf("Evaluation profile: %s\n", get_evaluation_profile());

    while (1) {
        Move user_move;
        Move engine_move;
        Move legal_moves[MAX_MOVES];
        char uci[6];
        char san[24];
        int legal_count;
        int score;
        long long started_ms;
        long long ended_ms;
        long long think_ms;

        board_print(&board);
        legal_count = generate_legal_moves(&board, legal_moves);
        if (legal_count == 0) {
            append_game_pgn_if_finished(
                debug_log,
                &game,
                &board,
                "minimax",
                get_evaluation_profile(),
                depth,
                "interactive"
            );
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

            snprintf(last_user_uci, sizeof(last_user_uci), "%s", input);
            build_san_move(&board, &user_move, san, sizeof(san));
            record_move_san(&game, san);
            apply_move(&board, &user_move, &board);
            continue;
        }

        request_id++;
        log_search_start(debug_log, request_id, "interactive", last_user_uci, depth);
        started_ms = now_ms();
        score = choose_best_move_with_debug(&board, depth, &engine_move, debug_log);
        ended_ms = now_ms();
        think_ms = (ended_ms >= started_ms) ? (ended_ms - started_ms) : 0;
        move_to_uci(&engine_move, uci);
        build_san_move(&board, &engine_move, san, sizeof(san));
        log_search_end(debug_log, request_id, uci, score, think_ms);
        printf("Engine plays: %s (score=%d)\n", uci, score);
        record_move_san(&game, san);
        apply_move(&board, &engine_move, &board);
    }

    if (debug_log) {
        fclose(debug_log);
    }

    return 0;
}
