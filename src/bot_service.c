#define _POSIX_C_SOURCE 200809L

#include <json-c/json.h>
#include <microhttpd.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BOT_PORT 5001
#define MAX_BODY_SIZE 4096
#define MAX_REPLY_SIZE 65536
#define MAX_TEST_OUTPUT_SIZE 131072
#define BOT_TYPE_MINIMAX "minimax"
#define BOT_TYPE_ITERDEEP "iterative-deepening"
#define EVAL_BASIC "basic"
#define EVAL_ADVANCED "advanced"
#define EVAL_TACTICAL "tactical"
#define EVAL_PHASED "phased"
#define DEBUG_LOG_DIR "logs"

struct connection_context {
    char *body;
    size_t body_size;
    size_t body_capacity;
    int handled;
};

static FILE *bot_stdin = NULL;
static FILE *bot_stdout = NULL;
static const char *bot_type = BOT_TYPE_MINIMAX;
static const char *bot_executable = "./chess-bot";
static int bot_depth = 4;
static int bot_debug = 0;
static char bot_debug_log_label[128] = "bot-search";
static char bot_debug_log_path[256] = "logs/bot-search.log";
static char bot_eval_profile[32] = EVAL_BASIC;
static pid_t bot_pid = -1;
extern char **environ;

static int ensure_debug_log_directory(void) {
    struct stat st;

    if (stat(DEBUG_LOG_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    if (mkdir(DEBUG_LOG_DIR, 0775) == 0) {
        return 1;
    }

    if (errno == EEXIST && stat(DEBUG_LOG_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    return 0;
}

static void sanitize_debug_log_label(const char *input, char *output, size_t output_size) {
    size_t i;
    size_t out_len = 0;

    if (!output || output_size == 0) {
        return;
    }

    if (!input || input[0] == '\0') {
        snprintf(output, output_size, "bot-search");
        return;
    }

    for (i = 0; input[i] != '\0' && out_len + 1 < output_size; i++) {
        unsigned char c = (unsigned char)input[i];
        if (isalnum(c)) {
            output[out_len++] = (char)tolower(c);
        } else if (c == '-' || c == '_') {
            output[out_len++] = (char)c;
        }
    }

    if (out_len == 0) {
        snprintf(output, output_size, "bot-search");
        return;
    }

    output[out_len] = '\0';
}

static void format_log_filename_timestamp(char *out, size_t out_size) {
    time_t now;
    struct tm tm_now;

    if (!out || out_size == 0) {
        return;
    }

    now = time(NULL);
#ifdef _WIN32
    if (localtime_s(&tm_now, &now) != 0 || strftime(out, out_size, "%Y%m%d-%H%M%S", &tm_now) == 0) {
        snprintf(out, out_size, "unknown-time");
    }
#else
    if (!localtime_r(&now, &tm_now) || strftime(out, out_size, "%Y%m%d-%H%M%S", &tm_now) == 0) {
        snprintf(out, out_size, "unknown-time");
    }
#endif
}

static void build_debug_log_path(
    const char *bot,
    const char *eval,
    int depth,
    const char *label,
    char *out_path,
    size_t out_path_size
) {
    char ts[32];

    if (!out_path || out_path_size == 0) {
        return;
    }

    format_log_filename_timestamp(ts, sizeof(ts));
    snprintf(
        out_path,
        out_path_size,
        "%s/%s_%s_%s_d%d_%s.log",
        DEBUG_LOG_DIR,
        ts,
        bot ? bot : "bot",
        eval ? eval : "eval",
        depth,
        label ? label : "session"
    );
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

static int parse_debug_value(const char *s) {
    if (!s || s[0] == '\0') {
        return 0;
    }
    return strcmp(s, "0") != 0;
}

static int is_valid_debug_log_path(const char *path) {
    size_t i;

    if (!path || path[0] == '\0') {
        return 0;
    }

    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == '\n' || path[i] == '\r') {
            return 0;
        }
    }

    return i < sizeof(bot_debug_log_label);
}

static int is_valid_bot_type(const char *value) {
    return value && (
        strcmp(value, BOT_TYPE_MINIMAX) == 0 ||
        strcmp(value, BOT_TYPE_ITERDEEP) == 0
    );
}

static int is_valid_eval_profile(const char *value) {
    return value && (
        strcmp(value, EVAL_BASIC) == 0 ||
        strcmp(value, EVAL_ADVANCED) == 0 ||
        strcmp(value, EVAL_TACTICAL) == 0 ||
        strcmp(value, EVAL_PHASED) == 0
    );
}

static const char *executable_for_bot_type(const char *value) {
    if (value && strcmp(value, BOT_TYPE_ITERDEEP) == 0) {
        return "./chess-bot-iterdeep";
    }
    return "./chess-bot";
}

static void stop_bot_process(void) {
    if (bot_stdin) {
        fclose(bot_stdin);
        bot_stdin = NULL;
    }
    if (bot_stdout) {
        fclose(bot_stdout);
        bot_stdout = NULL;
    }
    if (bot_pid > 0) {
        kill(bot_pid, SIGTERM);
        waitpid(bot_pid, NULL, 0);
        bot_pid = -1;
    }
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
    posix_spawn_file_actions_t actions;
    char *const argv[] = {
        (char *)bot_executable,
        "--protocol",
        (char *)depth_arg,
        NULL
    };
    int spawn_status;
    pid_t pid;

    if (setenv("BOT_DEBUG", bot_debug ? "1" : "0", 1) != 0) {
        return 0;
    }
    if (bot_debug) {
        if (!ensure_debug_log_directory()) {
            return 0;
        }
        build_debug_log_path(
            bot_type,
            bot_eval_profile,
            bot_depth,
            bot_debug_log_label,
            bot_debug_log_path,
            sizeof(bot_debug_log_path)
        );
        if (setenv("BOT_DEBUG_LOG", bot_debug_log_path, 1) != 0) {
            return 0;
        }
    } else {
        unsetenv("BOT_DEBUG_LOG");
    }
    if (setenv("BOT_EVAL", bot_eval_profile, 1) != 0) {
        return 0;
    }

    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        return 0;
    }

    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return 0;
    }

    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, to_child[1]);
    posix_spawn_file_actions_addclose(&actions, from_child[0]);
    posix_spawn_file_actions_addclose(&actions, to_child[0]);
    posix_spawn_file_actions_addclose(&actions, from_child[1]);

    spawn_status = posix_spawn(&pid, bot_executable, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);

    if (spawn_status != 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return 0;
    }

    bot_pid = pid;

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

static int apply_runtime_bot_config(
    const char *requested_bot_type,
    int requested_depth,
    int requested_debug,
    const char *requested_debug_log_label,
    const char *requested_eval_profile
) {
    char depth_arg[16];
    int needs_restart;

    if (!requested_bot_type || !is_valid_bot_type(requested_bot_type)) {
        return 0;
    }
    if (requested_depth < 1 || requested_depth > 8) {
        return 0;
    }
    if (requested_debug_log_label && !is_valid_debug_log_path(requested_debug_log_label)) {
        return 0;
    }
    if (!requested_eval_profile || !is_valid_eval_profile(requested_eval_profile)) {
        return 0;
    }

    needs_restart = (
        strcmp(bot_type, requested_bot_type) != 0 ||
        requested_depth != bot_depth ||
        requested_debug != bot_debug ||
        strcmp(bot_debug_log_label, requested_debug_log_label ? requested_debug_log_label : bot_debug_log_label) != 0 ||
        strcmp(bot_eval_profile, requested_eval_profile) != 0
    );
    if (!needs_restart) {
        return 1;
    }

    stop_bot_process();

    bot_type = (strcmp(requested_bot_type, BOT_TYPE_ITERDEEP) == 0)
        ? BOT_TYPE_ITERDEEP
        : BOT_TYPE_MINIMAX;
    bot_executable = executable_for_bot_type(bot_type);
    bot_depth = requested_depth;
    bot_debug = requested_debug ? 1 : 0;
    if (requested_debug_log_label) {
        sanitize_debug_log_label(requested_debug_log_label, bot_debug_log_label, sizeof(bot_debug_log_label));
    }
    snprintf(bot_eval_profile, sizeof(bot_eval_profile), "%s", requested_eval_profile);

    snprintf(depth_arg, sizeof(depth_arg), "%d", bot_depth);
    if (!start_bot_process(depth_arg)) {
        return 0;
    }

    fprintf(
        stderr,
        "Bot reconfigured: type=%s depth=%d eval=%s debug=%d log=%s\n",
        bot_type,
        bot_depth,
        bot_eval_profile,
        bot_debug,
        bot_debug_log_path
    );
    return 1;
}

static int extract_json_string_field(
    const char *body,
    const char *key,
    char *out,
    size_t out_size
) {
    const char *key_pos;
    const char *colon;
    const char *quote1;
    const char *quote2;
    size_t len;

    if (!body || !key || !out || out_size == 0) {
        return -1;
    }

    key_pos = strstr(body, key);
    if (!key_pos) {
        return 0;
    }

    colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }

    quote1 = strchr(colon, '"');
    if (!quote1) {
        return -1;
    }

    quote2 = strchr(quote1 + 1, '"');
    if (!quote2) {
        return -1;
    }

    len = (size_t)(quote2 - (quote1 + 1));
    if (len == 0 || len >= out_size) {
        return -1;
    }

    memcpy(out, quote1 + 1, len);
    out[len] = '\0';
    return 1;
}

static int extract_json_int_field(const char *body, const char *key, int *out_value) {
    const char *key_pos;
    const char *colon;
    char *endptr;
    long value;

    if (!body || !key || !out_value) {
        return -1;
    }

    key_pos = strstr(body, key);
    if (!key_pos) {
        return 0;
    }

    colon = strchr(key_pos, ':');
    if (!colon) {
        return -1;
    }

    value = strtol(colon + 1, &endptr, 10);
    if (colon + 1 == endptr) {
        return -1;
    }

    *out_value = (int)value;
    return 1;
}

static int extract_json_bool_field(const char *body, const char *key, int *out_value) {
    const char *key_pos;
    const char *cursor;

    if (!body || !key || !out_value) {
        return -1;
    }

    key_pos = strstr(body, key);
    if (!key_pos) {
        return 0;
    }

    cursor = strchr(key_pos, ':');
    if (!cursor) {
        return -1;
    }
    cursor++;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
        cursor++;
    }

    if (strncmp(cursor, "true", 4) == 0) {
        *out_value = 1;
        return 1;
    }
    if (strncmp(cursor, "false", 5) == 0) {
        *out_value = 0;
        return 1;
    }
    if (*cursor == '0') {
        *out_value = 0;
        return 1;
    }
    if (*cursor >= '1' && *cursor <= '9') {
        *out_value = 1;
        return 1;
    }

    return -1;
}

static int parse_move_or_reset_config(
    const char *json_body,
    const char **out_bot_type,
    int *out_depth,
    int *out_debug_enabled,
    char *out_debug_log_path,
    size_t out_debug_log_path_size,
    char *out_eval_profile,
    size_t out_eval_profile_size,
    char *error,
    size_t error_size
) {
    char parsed_bot[64];
    char parsed_log_path[256];
    char parsed_eval_profile[32];
    int parsed_depth;
    int parsed_debug;
    int field_status;

    if (out_bot_type) {
        *out_bot_type = bot_type;
    }
    if (out_depth) {
        *out_depth = bot_depth;
    }
    if (out_debug_enabled) {
        *out_debug_enabled = bot_debug;
    }
    if (out_debug_log_path && out_debug_log_path_size > 0) {
        snprintf(out_debug_log_path, out_debug_log_path_size, "%s", bot_debug_log_label);
    }
    if (out_eval_profile && out_eval_profile_size > 0) {
        snprintf(out_eval_profile, out_eval_profile_size, "%s", bot_eval_profile);
    }

    if (!json_body || json_body[0] == '\0') {
        return 1;
    }

    field_status = extract_json_string_field(json_body, "\"bot\"", parsed_bot, sizeof(parsed_bot));
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'bot' must be a string");
        }
        return 0;
    }
    if (field_status > 0) {
        if (!is_valid_bot_type(parsed_bot)) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "Unsupported bot type");
            }
            return 0;
        }

        if (out_bot_type) {
            *out_bot_type = (strcmp(parsed_bot, BOT_TYPE_ITERDEEP) == 0)
                ? BOT_TYPE_ITERDEEP
                : BOT_TYPE_MINIMAX;
        }
    }

    field_status = extract_json_int_field(json_body, "\"depth\"", &parsed_depth);
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'depth' must be an integer between 1 and 8");
        }
        return 0;
    }
    if (field_status > 0) {
        if (parsed_depth < 1 || parsed_depth > 8) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "'depth' must be between 1 and 8");
            }
            return 0;
        }

        if (out_depth) {
            *out_depth = parsed_depth;
        }
    }

    field_status = extract_json_bool_field(json_body, "\"debug\"", &parsed_debug);
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'debug' must be a boolean or integer");
        }
        return 0;
    }
    if (field_status > 0 && out_debug_enabled) {
        *out_debug_enabled = parsed_debug ? 1 : 0;
    }

    field_status = extract_json_string_field(json_body, "\"debugLog\"", parsed_log_path, sizeof(parsed_log_path));
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'debugLog' must be a non-empty string");
        }
        return 0;
    }
    if (field_status > 0) {
        if (!is_valid_debug_log_path(parsed_log_path)) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "'debugLog' path is invalid or too long");
            }
            return 0;
        }
        if (out_debug_log_path && out_debug_log_path_size > 0) {
            snprintf(out_debug_log_path, out_debug_log_path_size, "%s", parsed_log_path);
        }
    }

    field_status = extract_json_string_field(json_body, "\"eval\"", parsed_eval_profile, sizeof(parsed_eval_profile));
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'eval' must be a string");
        }
        return 0;
    }
    if (field_status > 0) {
        if (!is_valid_eval_profile(parsed_eval_profile)) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "'eval' must be one of: basic, advanced, tactical, phased");
            }
            return 0;
        }
        if (out_eval_profile && out_eval_profile_size > 0) {
            snprintf(out_eval_profile, out_eval_profile_size, "%s", parsed_eval_profile);
        }
    }

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

static int handle_get_config(struct MHD_Connection *connection) {
    json_object *payload = json_object_new_object();

    json_object_object_add(payload, "bot", json_object_new_string(bot_type));
    json_object_object_add(payload, "depth", json_object_new_int(bot_depth));
    json_object_object_add(payload, "eval", json_object_new_string(bot_eval_profile));
    json_object_object_add(payload, "debug", json_object_new_boolean(bot_debug));
    json_object_object_add(payload, "debugLog", json_object_new_string(bot_debug_log_label));
    json_object_object_add(payload, "debugLogFile", json_object_new_string(bot_debug_log_path));
    json_object_object_add(payload, "executable", json_object_new_string(bot_executable));

    return send_json_response(connection, payload, MHD_HTTP_OK);
}

static int is_valid_pgn_tag_value(const char *value) {
    size_t i;

    if (!value) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; i++) {
        if (value[i] == '\n' || value[i] == '\r') {
            return 0;
        }
    }

    return i < 512;
}

static void write_escaped_pgn_tag_value(FILE *fp, const char *value) {
    size_t i;

    if (!fp || !value) {
        return;
    }

    for (i = 0; value[i] != '\0'; i++) {
        if (value[i] == '\\' || value[i] == '"') {
            fputc('\\', fp);
        }
        fputc((unsigned char)value[i], fp);
    }
}

static int append_optional_pgn_tag(FILE *fp, const char *key, const char *value, int *written_count) {
    if (!fp || !key || !value || !written_count) {
        return 1;
    }

    if (value[0] == '\0') {
        return 1;
    }

    if (!is_valid_pgn_tag_value(value)) {
        return 0;
    }

    fprintf(fp, "[%s \"", key);
    write_escaped_pgn_tag_value(fp, value);
    fprintf(fp, "\"]\n");
    (*written_count)++;
    return 1;
}

static int handle_append_pgn_tags(struct MHD_Connection *connection, const char *body) {
    char event[256] = "";
    char site[256] = "";
    char white[256] = "";
    char black[256] = "";
    char round[64] = "";
    char time_control[128] = "";
    char white_elo[64] = "";
    char black_elo[64] = "";
    char termination[256] = "";
    char eco[64] = "";
    char end_time[128] = "";
    char link[512] = "";
    int written_count = 0;
    FILE *fp;
    json_object *payload;

    if (!body || body[0] == '\0') {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "Missing JSON body");
    }

    if (!ensure_debug_log_directory()) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Logs directory is unavailable");
    }

    fp = fopen(bot_debug_log_path, "a");
    if (!fp) {
        return send_error_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Unable to open current debug log file");
    }

    extract_json_string_field(body, "\"event\"", event, sizeof(event));
    extract_json_string_field(body, "\"site\"", site, sizeof(site));
    extract_json_string_field(body, "\"white\"", white, sizeof(white));
    extract_json_string_field(body, "\"black\"", black, sizeof(black));
    extract_json_string_field(body, "\"round\"", round, sizeof(round));
    extract_json_string_field(body, "\"timeControl\"", time_control, sizeof(time_control));
    extract_json_string_field(body, "\"whiteElo\"", white_elo, sizeof(white_elo));
    extract_json_string_field(body, "\"blackElo\"", black_elo, sizeof(black_elo));
    extract_json_string_field(body, "\"termination\"", termination, sizeof(termination));
    extract_json_string_field(body, "\"eco\"", eco, sizeof(eco));
    extract_json_string_field(body, "\"endTime\"", end_time, sizeof(end_time));
    extract_json_string_field(body, "\"link\"", link, sizeof(link));

    fprintf(fp, "\n[OptionalPGNTags]\n");

    if (!append_optional_pgn_tag(fp, "Event", event, &written_count) ||
        !append_optional_pgn_tag(fp, "Site", site, &written_count) ||
        !append_optional_pgn_tag(fp, "White", white, &written_count) ||
        !append_optional_pgn_tag(fp, "Black", black, &written_count) ||
        !append_optional_pgn_tag(fp, "Round", round, &written_count) ||
        !append_optional_pgn_tag(fp, "TimeControl", time_control, &written_count) ||
        !append_optional_pgn_tag(fp, "WhiteElo", white_elo, &written_count) ||
        !append_optional_pgn_tag(fp, "BlackElo", black_elo, &written_count) ||
        !append_optional_pgn_tag(fp, "Termination", termination, &written_count) ||
        !append_optional_pgn_tag(fp, "ECO", eco, &written_count) ||
        !append_optional_pgn_tag(fp, "EndTime", end_time, &written_count) ||
        !append_optional_pgn_tag(fp, "Link", link, &written_count)) {
        fclose(fp);
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, "One or more PGN tag values are invalid");
    }

    if (written_count == 0) {
        fprintf(fp, "(no optional tags provided)\n");
    }

    fflush(fp);
    fclose(fp);

    payload = json_object_new_object();
    json_object_object_add(payload, "ok", json_object_new_boolean(1));
    json_object_object_add(payload, "writtenTags", json_object_new_int(written_count));
    json_object_object_add(payload, "logFile", json_object_new_string(bot_debug_log_path));
    return send_json_response(connection, payload, MHD_HTTP_OK);
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

static int append_to_buffer(char *dest, size_t dest_size, size_t *offset, const char *chunk) {
    size_t remaining;
    size_t len;

    if (!dest || !offset || !chunk || *offset >= dest_size) {
        return 0;
    }

    remaining = dest_size - *offset - 1;
    len = strlen(chunk);
    if (len > remaining) {
        len = remaining;
    }

    memcpy(dest + *offset, chunk, len);
    *offset += len;
    dest[*offset] = '\0';

    return remaining > 0;
}

static int run_command_capture(const char *command, char *output, size_t output_size, int *exit_code) {
    FILE *pipe;
    char line[1024];
    size_t offset = 0;
    int status;

    if (!command || !output || output_size == 0 || !exit_code) {
        return 0;
    }

    output[0] = '\0';
    pipe = popen(command, "r");
    if (!pipe) {
        return 0;
    }

    while (fgets(line, sizeof(line), pipe) != NULL) {
        if (!append_to_buffer(output, output_size, &offset, line)) {
            break;
        }
    }

    status = pclose(pipe);
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    } else {
        *exit_code = 1;
    }

    return 1;
}

static int parse_test_request(
    const char *body,
    const char **requested_bot,
    const char **suite,
    int *perft_max_depth,
    int *perft_strict,
    char *error,
    size_t error_size
) {
    char parsed_bot[64];
    char parsed_suite[32];
    int parsed_int;
    int field_status;

    *requested_bot = bot_type;
    *suite = "all";
    *perft_max_depth = 5;
    *perft_strict = 0;

    if (!body || body[0] == '\0') {
        return 1;
    }

    field_status = extract_json_string_field(body, "\"bot\"", parsed_bot, sizeof(parsed_bot));
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'bot' must be a string");
        }
        return 0;
    }
    if (field_status > 0) {
        if (!is_valid_bot_type(parsed_bot)) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "Unsupported bot type");
            }
            return 0;
        }

        *requested_bot = (strcmp(parsed_bot, BOT_TYPE_ITERDEEP) == 0)
            ? BOT_TYPE_ITERDEEP
            : BOT_TYPE_MINIMAX;
    }

    field_status = extract_json_string_field(body, "\"suite\"", parsed_suite, sizeof(parsed_suite));
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'suite' must be a string");
        }
        return 0;
    }
    if (field_status > 0) {
        if (
            strcmp(parsed_suite, "unit") != 0 &&
            strcmp(parsed_suite, "perft") != 0 &&
            strcmp(parsed_suite, "all") != 0
        ) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "'suite' must be one of: unit, perft, all");
            }
            return 0;
        }

        if (strcmp(parsed_suite, "unit") == 0) {
            *suite = "unit";
        } else if (strcmp(parsed_suite, "perft") == 0) {
            *suite = "perft";
        } else {
            *suite = "all";
        }
    }

    field_status = extract_json_int_field(body, "\"perftMaxDepth\"", &parsed_int);
    if (field_status < 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "'perftMaxDepth' must be an integer between 1 and 6");
        }
        return 0;
    }
    if (field_status > 0) {
        *perft_max_depth = parsed_int;
        if (*perft_max_depth < 1 || *perft_max_depth > 6) {
            if (error && error_size > 0) {
                snprintf(error, error_size, "'perftMaxDepth' must be between 1 and 6");
            }
            return 0;
        }
    }

    field_status = extract_json_int_field(body, "\"perftStrict\"", &parsed_int);
    if (field_status > 0) {
        *perft_strict = parsed_int ? 1 : 0;
    } else {
        char parsed_bool[16];
        field_status = extract_json_string_field(body, "\"perftStrict\"", parsed_bool, sizeof(parsed_bool));
        if (field_status > 0) {
            if (strcmp(parsed_bool, "true") == 0) {
                *perft_strict = 1;
            } else if (strcmp(parsed_bool, "false") == 0) {
                *perft_strict = 0;
            } else {
                if (error && error_size > 0) {
                    snprintf(error, error_size, "'perftStrict' must be a boolean or integer");
                }
                return 0;
            }
        } else if (strstr(body, "\"perftStrict\":true") != NULL) {
            *perft_strict = 1;
        } else if (strstr(body, "\"perftStrict\":false") != NULL) {
            *perft_strict = 0;
        } else {
            if (field_status < 0) {
                if (error && error_size > 0) {
                    snprintf(error, error_size, "'perftStrict' must be a boolean or integer");
                }
                return 0;
            }
        }
    }
    return 1;
}

static void append_test_result(
    json_object *results,
    const char *name,
    const char *command,
    int *all_passed
) {
    char *output;
    int exit_code = 1;
    int passed;
    json_object *entry = json_object_new_object();

    output = malloc(MAX_TEST_OUTPUT_SIZE);
    if (!output) {
        output = "Failed to allocate test output buffer";
        exit_code = 1;
        passed = 0;
        *all_passed = 0;

        json_object_object_add(entry, "name", json_object_new_string(name));
        json_object_object_add(entry, "command", json_object_new_string(command));
        json_object_object_add(entry, "exitCode", json_object_new_int(exit_code));
        json_object_object_add(entry, "passed", json_object_new_boolean(passed));
        json_object_object_add(entry, "output", json_object_new_string(output));
        json_object_array_add(results, entry);
        return;
    }

    if (!run_command_capture(command, output, MAX_TEST_OUTPUT_SIZE, &exit_code)) {
        snprintf(output, MAX_TEST_OUTPUT_SIZE, "Failed to execute command: %s", command);
        exit_code = 1;
    }

    passed = (exit_code == 0);
    if (!passed) {
        *all_passed = 0;
    }

    json_object_object_add(entry, "name", json_object_new_string(name));
    json_object_object_add(entry, "command", json_object_new_string(command));
    json_object_object_add(entry, "exitCode", json_object_new_int(exit_code));
    json_object_object_add(entry, "passed", json_object_new_boolean(passed));
    json_object_object_add(entry, "output", json_object_new_string(output));

    json_object_array_add(results, entry);
    free(output);
}

static int handle_run_tests(struct MHD_Connection *connection, const char *body) {
    char error[128];
    const char *requested_bot;
    const char *suite;
    const char *unit_binary;
    const char *perft_binary;
    char perft_command[256];
    int perft_max_depth;
    int perft_strict;
    int all_passed = 1;
    json_object *payload;
    json_object *results;

    if (!parse_test_request(
            body,
            &requested_bot,
            &suite,
            &perft_max_depth,
            &perft_strict,
            error,
            sizeof(error)
        )) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, error);
    }

    if (strcmp(requested_bot, BOT_TYPE_ITERDEEP) == 0) {
        unit_binary = "./test-engine-core-iterdeep";
        perft_binary = "./test-perft-iterdeep";
    } else {
        unit_binary = "./test-engine-core-minimax";
        perft_binary = "./test-perft-minimax";
    }

    payload = json_object_new_object();
    results = json_object_new_array();

    json_object_object_add(payload, "bot", json_object_new_string(requested_bot));
    json_object_object_add(payload, "suite", json_object_new_string(suite));
    json_object_object_add(payload, "perftMaxDepth", json_object_new_int(perft_max_depth));
    json_object_object_add(payload, "perftStrict", json_object_new_boolean(perft_strict));

    if (strcmp(suite, "unit") == 0 || strcmp(suite, "all") == 0) {
        char unit_command[128];
        snprintf(unit_command, sizeof(unit_command), "%s 2>&1", unit_binary);
        append_test_result(results, "unit", unit_command, &all_passed);
    }

    if (strcmp(suite, "perft") == 0 || strcmp(suite, "all") == 0) {
        snprintf(
            perft_command,
            sizeof(perft_command),
            "PERFT_MAX_DEPTH=%d PERFT_STRICT=%d %s 2>&1",
            perft_max_depth,
            perft_strict,
            perft_binary
        );
        append_test_result(results, "perft", perft_command, &all_passed);
    }

    json_object_object_add(payload, "passed", json_object_new_boolean(all_passed));
    json_object_object_add(payload, "tests", results);

    return send_json_response(connection, payload, all_passed ? MHD_HTTP_OK : MHD_HTTP_BAD_REQUEST);
}

static int handle_move(struct MHD_Connection *connection, const char *body) {
    char uci[8];
    char command[32];
    char reply[MAX_REPLY_SIZE];
    const char *requested_bot_type;
    int requested_depth;
    int requested_debug;
    char requested_debug_log_path[256];
    char requested_eval_profile[32];
    char error[128];

    if (!parse_move_or_reset_config(
            body,
            &requested_bot_type,
            &requested_depth,
            &requested_debug,
            requested_debug_log_path,
            sizeof(requested_debug_log_path),
            requested_eval_profile,
            sizeof(requested_eval_profile),
            error,
            sizeof(error)
        )) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, error);
    }

    if (!apply_runtime_bot_config(
            requested_bot_type,
            requested_depth,
            requested_debug,
            requested_debug_log_path,
            requested_eval_profile
        )) {
        return send_error_response(connection, MHD_HTTP_BAD_GATEWAY, "Failed to apply bot configuration");
    }

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

static int handle_reset(struct MHD_Connection *connection, const char *body) {
    char reply[MAX_REPLY_SIZE];
    const char *requested_bot_type;
    int requested_depth;
    int requested_debug;
    char requested_debug_log_path[256];
    char requested_eval_profile[32];
    char error[128];

    if (!parse_move_or_reset_config(
            body,
            &requested_bot_type,
            &requested_depth,
            &requested_debug,
            requested_debug_log_path,
            sizeof(requested_debug_log_path),
            requested_eval_profile,
            sizeof(requested_eval_profile),
            error,
            sizeof(error)
        )) {
        return send_error_response(connection, MHD_HTTP_BAD_REQUEST, error);
    }

    if (!apply_runtime_bot_config(
            requested_bot_type,
            requested_depth,
            requested_debug,
            requested_debug_log_path,
            requested_eval_profile
        )) {
        return send_error_response(connection, MHD_HTTP_BAD_GATEWAY, "Failed to apply bot configuration");
    }

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

    if (strcmp(method, "GET") == 0 && strcmp(url, "/bot/config") == 0) {
        return handle_get_config(connection);
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
            return handle_reset(connection, ctx->body);
        }
        if (strcmp(url, "/bot/tests") == 0) {
            return handle_run_tests(connection, ctx->body);
        }
        if (strcmp(url, "/bot/pgn-tags") == 0) {
            return handle_append_pgn_tags(connection, ctx->body);
        }
    }

    return send_error_response(connection, MHD_HTTP_NOT_FOUND, "Not found");
}

int main(int argc, char **argv) {
    struct MHD_Daemon *daemon;
    const char *env_depth = getenv("BOT_DEPTH");
    const char *env_bot_type = getenv("BOT_TYPE");
    const char *env_eval_profile = getenv("BOT_EVAL");
    const char *env_debug = getenv("BOT_DEBUG");
    const char *env_debug_log = getenv("BOT_DEBUG_LOG");
    char depth_arg[16];
    int parsed_depth;

    if (env_bot_type && env_bot_type[0] != '\0') {
        if (!is_valid_bot_type(env_bot_type)) {
            fprintf(stderr, "Unsupported BOT_TYPE '%s'\n", env_bot_type);
            return 1;
        }
        bot_type = (strcmp(env_bot_type, BOT_TYPE_ITERDEEP) == 0)
            ? BOT_TYPE_ITERDEEP
            : BOT_TYPE_MINIMAX;
    }

    if (env_depth && parse_depth_value(env_depth, &parsed_depth)) {
        bot_depth = parsed_depth;
    }
    if (env_eval_profile && is_valid_eval_profile(env_eval_profile)) {
        snprintf(bot_eval_profile, sizeof(bot_eval_profile), "%s", env_eval_profile);
    }
    bot_debug = parse_debug_value(env_debug);
    if (env_debug_log && env_debug_log[0] != '\0' && is_valid_debug_log_path(env_debug_log)) {
        sanitize_debug_log_label(env_debug_log, bot_debug_log_label, sizeof(bot_debug_log_label));
    }

    if (argc > 1) {
        if (!parse_depth_value(argv[1], &parsed_depth)) {
            fprintf(stderr, "Usage: %s [depth 1-8]\n", argv[0]);
            return 1;
        }
        bot_depth = parsed_depth;
    }

    bot_executable = executable_for_bot_type(bot_type);
    snprintf(depth_arg, sizeof(depth_arg), "%d", bot_depth);

    if (!start_bot_process(depth_arg)) {
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
    printf("Bot type: %s\n", bot_type);
    printf("Bot executable: %s\n", bot_executable);
    printf("Using chess-bot depth %d\n", bot_depth);
    printf("Evaluation profile: %s\n", bot_eval_profile);
    printf("Debug logging: %s (file=%s)\n", bot_debug ? "enabled" : "disabled", bot_debug_log_path);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    while (1) {
        sleep(60);
    }

    MHD_stop_daemon(daemon);
    stop_bot_process();
    return 0;
}