#define _POSIX_C_SOURCE 200809L

#include <json-c/json.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BOT_PORT 5001
#define MAX_BODY_SIZE 4096
#define MAX_REPLY_SIZE 65536

struct connection_context {
    char *body;
    size_t body_size;
    size_t body_capacity;
    int handled;
};

static FILE *bot_stdin = NULL;
static FILE *bot_stdout = NULL;

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

static int send_json_response(struct MHD_Connection *connection, json_object *json_obj, int status_code) {
    const char *json_str = json_object_to_json_string(json_obj);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_str),
        (void *)json_str,
        MHD_RESPMEM_MUST_COPY
    );
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    json_object_put(json_obj);
    return ret;
}

static int send_raw_json_response(struct MHD_Connection *connection, const char *json_str, int status_code) {
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_str),
        (void *)json_str,
        MHD_RESPMEM_MUST_COPY
    );
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    return ret;
}

static int send_error_response(struct MHD_Connection *connection, int status_code, const char *message) {
    json_object *error = json_object_new_object();
    json_object_object_add(error, "error", json_object_new_string(message));
    return send_json_response(connection, error, status_code);
}

static int start_bot_process(const char *depth_arg) {
    int to_child[2];
    int from_child[2];
    pid_t pid;

    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return 0;
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);

        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);

        execl("./chess-bot", "./chess-bot", "--protocol", depth_arg, (char *)NULL);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);

    bot_stdin = fdopen(to_child[1], "w");
    bot_stdout = fdopen(from_child[0], "r");

    if (!bot_stdin || !bot_stdout) {
        if (bot_stdin) {
            fclose(bot_stdin);
            bot_stdin = NULL;
        }
        if (bot_stdout) {
            fclose(bot_stdout);
            bot_stdout = NULL;
        }
        return 0;
    }

    setvbuf(bot_stdin, NULL, _IOLBF, 0);
    setvbuf(bot_stdout, NULL, _IOLBF, 0);
    return 1;
}

static int bot_command(const char *command, char *reply, size_t reply_size) {
    if (!bot_stdin || !bot_stdout) {
        return 0;
    }

    if (fprintf(bot_stdin, "%s\n", command) < 0) {
        return 0;
    }
    fflush(bot_stdin);

    if (!fgets(reply, (int)reply_size, bot_stdout)) {
        return 0;
    }

    {
        size_t len = strlen(reply);
        while (len > 0 && (reply[len - 1] == '\n' || reply[len - 1] == '\r')) {
            reply[len - 1] = '\0';
            len--;
        }
    }

    return 1;
}

static int handle_get_board(struct MHD_Connection *connection) {
    char reply[MAX_REPLY_SIZE];

    if (!bot_command("board", reply, sizeof(reply))) {
        return send_error_response(connection, MHD_HTTP_BAD_GATEWAY, "Bot process unavailable");
    }

    return send_raw_json_response(connection, reply, MHD_HTTP_OK);
}

static int extract_uci_from_body(const char *body, char *uci_buf, size_t uci_buf_size) {
    const char *key;
    const char *colon;
    const char *quote1;
    const char *quote2;
    size_t uci_len;

    if (!body) {
        return 0;
    }

    key = strstr(body, "\"uci\"");
    if (!key) {
        return 0;
    }

    colon = strchr(key, ':');
    if (!colon) {
        return 0;
    }

    quote1 = strchr(colon, '"');
    if (!quote1) {
        return 0;
    }

    quote2 = strchr(quote1 + 1, '"');
    if (!quote2) {
        return 0;
    }

    uci_len = (size_t)(quote2 - (quote1 + 1));
    if (uci_len < 4 || uci_len > 5 || uci_len >= uci_buf_size) {
        return 0;
    }

    memcpy(uci_buf, quote1 + 1, uci_len);
    uci_buf[uci_len] = '\0';
    return 1;
}

static int handle_move(struct MHD_Connection *connection, const char *body) {
    char uci[8];
    char command[32];
    char reply[MAX_REPLY_SIZE];

    if (!extract_uci_from_body(body, uci, sizeof(uci))) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Invalid or missing uci field");
    }

    snprintf(command, sizeof(command), "move %s", uci);
    if (!bot_command(command, reply, sizeof(reply))) {
        return send_error_response(connection, MHD_HTTP_BAD_GATEWAY, "Bot process unavailable");
    }

    if (strstr(reply, "\"error\"") != NULL) {
        return send_raw_json_response(connection, reply, MHD_HTTP_BAD_REQUEST);
    }

    return send_raw_json_response(connection, reply, MHD_HTTP_OK);
}

static int handle_reset(struct MHD_Connection *connection) {
    char reply[MAX_REPLY_SIZE];

    if (!bot_command("reset", reply, sizeof(reply))) {
        return send_error_response(connection, MHD_HTTP_BAD_GATEWAY, "Bot process unavailable");
    }

    return send_raw_json_response(connection, reply, MHD_HTTP_OK);
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
    struct connection_context *ctx;

    (void)cls;
    (void)version;

    if (strcmp(method, "GET") == 0 && strcmp(url, "/bot/board") == 0) {
        return handle_get_board(connection);
    }

    if (*con_cls == NULL) {
        ctx = calloc(1, sizeof(struct connection_context));
        if (!ctx) {
            return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
        }
        *con_cls = ctx;
        return MHD_YES;
    }

    ctx = (struct connection_context *)*con_cls;

    if (strcmp(method, "POST") == 0) {
        if (*upload_data_size > 0) {
            size_t needed = ctx->body_size + *upload_data_size + 1;
            if (needed > MAX_BODY_SIZE) {
                return send_error_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
            }

            if (ctx->body_capacity < needed) {
                size_t new_capacity = needed * 2;
                char *new_body = realloc(ctx->body, new_capacity);
                if (!new_body) {
                    return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
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

        if (!ctx->body) {
            ctx->body = malloc(1);
            if (!ctx->body) {
                return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
            }
            ctx->body[0] = '\0';
        } else {
            ctx->body[ctx->body_size] = '\0';
        }

        if (strcmp(url, "/bot/move") == 0) {
            return handle_move(connection, ctx->body);
        }
        if (strcmp(url, "/bot/reset") == 0) {
            return handle_reset(connection);
        }
    }

    return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Not found");
}

int main(int argc, char **argv) {
    struct MHD_Daemon *daemon;
    const char *depth = "4";
    const char *env_depth = getenv("BOT_DEPTH");
    int parsed_depth;

    if (env_depth && parse_depth_value(env_depth, &parsed_depth)) {
        depth = env_depth;
    }

    if (argc > 1) {
        if (!parse_depth_value(argv[1], &parsed_depth)) {
            fprintf(stderr, "Usage: %s [depth 1-8]\n", argv[0]);
            return 1;
        }
        depth = argv[1];
    }

    if (!start_bot_process(depth)) {
        fprintf(stderr, "Failed to start chess-bot process\n");
        return 1;
    }

    daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        BOT_PORT,
        NULL,
        NULL,
        &request_handler,
        NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END
    );

    if (daemon == NULL) {
        fprintf(stderr, "Failed to start bot daemon on port %d\n", BOT_PORT);
        return 1;
    }

    printf("Chess Bot adapter running on http://0.0.0.0:%d\n", BOT_PORT);
    printf("Using chess-bot --protocol depth %s\n", depth);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    while (1) {
        sleep(60);
    }

    MHD_stop_daemon(daemon);
    return 0;
}
