#include "chess.h"

#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <json-c/json.h>
#include <unistd.h>

#define PORT 5000
#define MAX_BODY_SIZE 4096

/* Per-connection context for accumulating POST body */
struct connection_context {
    char *body;
    size_t body_size;
    size_t body_capacity;
    int handled;
};

static Board current_board;

static json_object *move_to_json(const Move *move) {
    json_object *obj = json_object_new_object();
    char uci[6];
    
    move_to_uci(move, uci);
    json_object_object_add(obj, "uci", json_object_new_string(uci));
    json_object_object_add(obj, "from", json_object_new_int(move->from));
    json_object_object_add(obj, "to", json_object_new_int(move->to));
    
    return obj;
}

static json_object *board_to_json(const Board *board) {
    json_object *obj = json_object_new_object();
    json_object *squares_arr = json_object_new_array();
    Move moves[MAX_MOVES];
    int move_count = generate_legal_moves(board, moves);
    json_object *moves_arr = json_object_new_array();
    int i;
    int in_check = is_in_check(board, board->side_to_move);
    
    for (i = 0; i < 64; i++) {
        json_object_array_add(squares_arr, json_object_new_int(board->squares[i]));
    }
    json_object_object_add(obj, "squares", squares_arr);
    
    json_object_object_add(obj, "side_to_move", json_object_new_int(board->side_to_move));
    json_object_object_add(obj, "castling_rights", json_object_new_int(board->castling_rights));
    json_object_object_add(obj, "en_passant_sq", json_object_new_int(board->en_passant_sq));
    
    for (i = 0; i < move_count; i++) {
        json_object_array_add(moves_arr, move_to_json(&moves[i]));
    }
    json_object_object_add(obj, "legal_moves", moves_arr);
    json_object_object_add(obj, "move_count", json_object_new_int(move_count));
    json_object_object_add(obj, "in_check", json_object_new_boolean(in_check != 0));
    
    return obj;
}

static void add_cors_headers(struct MHD_Response *response) {
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
}

static int send_json_response(struct MHD_Connection *connection, json_object *json_obj, int status_code) {
    const char *json_str = json_object_to_json_string(json_obj);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_str),
        (void *)json_str,
        MHD_RESPMEM_MUST_COPY
    );
    MHD_add_response_header(response, "Content-Type", "application/json");
    add_cors_headers(response);
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    json_object_put(json_obj);
    return ret;
}

static int handle_get_board(struct MHD_Connection *connection) {
    json_object *response = board_to_json(&current_board);
    return send_json_response(connection, response, MHD_HTTP_OK);
}

static int handle_move(struct MHD_Connection *connection, const char *body) {
    const char *key;
    const char *colon;
    const char *quote1;
    const char *quote2;
    char uci_buf[8];
    size_t uci_len;
    const char *uci;
    Move move;
    Board next_board;

    if (!body) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Empty request body"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    key = strstr(body, "\"uci\"");
    if (!key) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Missing uci field"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    colon = strchr(key, ':');
    if (!colon) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Invalid request body"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    quote1 = strchr(colon, '"');
    if (!quote1) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Invalid request body"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    quote2 = strchr(quote1 + 1, '"');
    if (!quote2) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Invalid request body"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    uci_len = (size_t)(quote2 - (quote1 + 1));
    if (uci_len < 4 || uci_len > 5 || uci_len >= sizeof(uci_buf)) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Invalid UCI move"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    memcpy(uci_buf, quote1 + 1, uci_len);
    uci_buf[uci_len] = '\0';
    uci = uci_buf;

    if (!uci || !parse_uci_move(&current_board, uci, &move)) {
        json_object *error = json_object_new_object();
        json_object_object_add(error, "error", json_object_new_string("Illegal move"));
        return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
    }

    apply_move(&current_board, &move, &next_board);
    current_board = next_board;

    /* Engine move */
    Move best;
    int depth = 4;
    int score = choose_best_move(&current_board, depth, &best);
    Board engine_next;
    apply_move(&current_board, &best, &engine_next);
    current_board = engine_next;

    json_object *response = board_to_json(&current_board);
    json_object_object_add(response, "engine_move", json_object_new_string("moved"));
    json_object_object_add(response, "engine_score", json_object_new_int(score));

    return send_json_response(connection, response, MHD_HTTP_OK);
}

static int handle_reset(struct MHD_Connection *connection) {
    board_set_startpos(&current_board);
    json_object *response = board_to_json(&current_board);
    return send_json_response(connection, response, MHD_HTTP_OK);
}

static void request_completed(
    void *cls,
    struct MHD_Connection *connection,
    void **con_cls,
    enum MHD_RequestTerminationCode toe
) {
    struct connection_context *ctx;

    (void)cls;
    (void)connection;
    (void)toe;

    if (!con_cls || !*con_cls) {
        return;
    }

    ctx = (struct connection_context *)*con_cls;
    free(ctx->body);
    free(ctx);
    *con_cls = NULL;
}

static enum MHD_Result request_handler(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
) {
    (void)cls;
    (void)version;

    struct connection_context *ctx;
    
    /* Handle CORS preflight requests */
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, (void *)"", MHD_RESPMEM_PERSISTENT);
        add_cors_headers(response);
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    /* Handle GET /api/board */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/board") == 0) {
        return handle_get_board(connection);
    }

    if (*con_cls == NULL) {
        ctx = calloc(1, sizeof(struct connection_context));
        if (!ctx) {
            json_object *error = json_object_new_object();
            json_object_object_add(error, "error", json_object_new_string("Out of memory"));
            return send_json_response(connection, error, MHD_HTTP_INTERNAL_SERVER_ERROR);
        }
        *con_cls = ctx;
        return MHD_YES;
    }

    ctx = (struct connection_context *)*con_cls;

    /* Handle POST requests - accumulate body data */
    if (strcmp(method, "POST") == 0) {
        /* Accumulate upload data */
        if (*upload_data_size > 0) {
            size_t needed = ctx->body_size + *upload_data_size + 1;
            if (needed > MAX_BODY_SIZE) {
                /* Body too large */
                json_object *error = json_object_new_object();
                json_object_object_add(error, "error", json_object_new_string("Request body too large"));
                return send_json_response(connection, error, MHD_HTTP_CONTENT_TOO_LARGE);
            }

            if (ctx->body_capacity < needed) {
                size_t new_capacity = needed * 2;
                char *new_body = realloc(ctx->body, new_capacity);
                if (!new_body) {
                    json_object *error = json_object_new_object();
                    json_object_object_add(error, "error", json_object_new_string("Out of memory"));
                    return send_json_response(connection, error, MHD_HTTP_INTERNAL_SERVER_ERROR);
                }
                ctx->body = new_body;
                ctx->body_capacity = new_capacity;
            }

            memcpy(ctx->body + ctx->body_size, upload_data, *upload_data_size);
            ctx->body_size += *upload_data_size;
            *upload_data_size = 0;

            return MHD_YES;
        }

        if (ctx->handled) {
            return MHD_YES;
        }
        ctx->handled = 1;

        /* All data received - process the request */
        int ret = MHD_NO;

        if (!ctx->body) {
            ctx->body = malloc(1);
            if (!ctx->body) {
                json_object *error = json_object_new_object();
                json_object_object_add(error, "error", json_object_new_string("Out of memory"));
                return send_json_response(connection, error, MHD_HTTP_INTERNAL_SERVER_ERROR);
            }
            ctx->body[0] = '\0';
        } else {
            ctx->body[ctx->body_size] = '\0';  /* Null-terminate */
        }

        if (strcmp(url, "/api/move") == 0) {
            size_t body_len = strlen(ctx->body);
            char *body_copy = malloc(body_len + 1);
            if (!body_copy) {
                json_object *error = json_object_new_object();
                json_object_object_add(error, "error", json_object_new_string("Out of memory"));
                return send_json_response(connection, error, MHD_HTTP_INTERNAL_SERVER_ERROR);
            }
            memcpy(body_copy, ctx->body, body_len + 1);
            ret = handle_move(connection, body_copy);
            free(body_copy);
        } else if (strcmp(url, "/api/reset") == 0) {
            ret = handle_reset(connection);
        } else {
            json_object *error = json_object_new_object();
            json_object_object_add(error, "error", json_object_new_string("Not found"));
            ret = send_json_response(connection, error, MHD_HTTP_NOT_FOUND);
        }

        return ret;
    }
    
    /* Method not allowed */
    json_object *error = json_object_new_object();
    json_object_object_add(error, "error", json_object_new_string("Method not allowed"));
    return send_json_response(connection, error, MHD_HTTP_BAD_REQUEST);
}

int main(int argc, char **argv) {
    struct MHD_Daemon *daemon;
    
    (void)argc;
    (void)argv;
    
    board_set_startpos(&current_board);
    
    daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        PORT,
        NULL,
        NULL,
        &request_handler,
        NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END
    );
    
    if (daemon == NULL) {
        fprintf(stderr, "Failed to start MHD daemon on port %d\n", PORT);
        return 1;
    }
    
    printf("Chess API server running on http://0.0.0.0:%d\n", PORT);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);
    
    /* Keep server running forever - sleep in a loop instead of waiting for input */
    while (1) {
        sleep(60);
    }
    
    MHD_stop_daemon(daemon);
    return 0;
}
