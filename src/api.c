#include <curl/curl.h>
#include <json-c/json.h>
#include <microhttpd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define API_PORT 5000
#define MAX_BODY_SIZE 4096
#define DEFAULT_BOT_BASE_URL "http://bot:5001"
#define DEFAULT_BOT_TIMEOUT_SECONDS 60L

struct connection_context {
    char *body;
    size_t body_size;
    size_t body_capacity;
    int handled;
};

struct http_buffer {
    char *data;
    size_t size;
    size_t capacity;
};

static const char *bot_base_url = DEFAULT_BOT_BASE_URL;
static long bot_timeout_seconds = DEFAULT_BOT_TIMEOUT_SECONDS;

static long parse_positive_long_or_default(const char *value, long fallback) {
    char *endptr;
    long parsed;

    if (!value || value[0] == '\0') {
        return fallback;
    }

    parsed = strtol(value, &endptr, 10);
    if (*endptr != '\0' || parsed <= 0) {
        return fallback;
    }

    return parsed;
}

static void add_cors_headers(struct MHD_Response *response) {
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
}

static int send_raw_json_response(
    struct MHD_Connection *connection,
    const char *json_body,
    int status_code
) {
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_body),
        (void *)json_body,
        MHD_RESPMEM_MUST_COPY
    );
    MHD_add_response_header(response, "Content-Type", "application/json");
    add_cors_headers(response);
    {
        int ret = MHD_queue_response(connection, status_code, response);
        MHD_destroy_response(response);
        return ret;
    }
}

static int send_error_json_response(
    struct MHD_Connection *connection,
    int status_code,
    const char *message
) {
    json_object *error = json_object_new_object();
    int ret;

    json_object_object_add(error, "error", json_object_new_string(message));
    ret = send_raw_json_response(connection, json_object_to_json_string(error), status_code);
    json_object_put(error);
    return ret;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userdata) {
    struct http_buffer *buffer = (struct http_buffer *)userdata;
    size_t incoming = size * nmemb;
    size_t needed;
    char *new_data;

    if (incoming == 0) {
        return 0;
    }

    needed = buffer->size + incoming + 1;
    if (buffer->capacity < needed) {
        size_t new_capacity = needed * 2;
        new_data = realloc(buffer->data, new_capacity);
        if (!new_data) {
            return 0;
        }
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    memcpy(buffer->data + buffer->size, contents, incoming);
    buffer->size += incoming;
    buffer->data[buffer->size] = '\0';

    return incoming;
}

static int forward_request(
    struct MHD_Connection *connection,
    const char *method,
    const char *path,
    const char *body,
    long timeout_seconds
) {
    CURL *curl;
    CURLcode curl_result;
    long status_code = 502;
    struct http_buffer response_buffer = {0};
    struct curl_slist *headers = NULL;
    char url[256];
    int result;

    if (snprintf(url, sizeof(url), "%s%s", bot_base_url, path) >= (int)sizeof(url)) {
        return send_error_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Target URL too long");
    }

    curl = curl_easy_init();
    if (!curl) {
        return send_error_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Failed to initialize HTTP client");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);

    if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body ? body : ""));
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }
    }

    curl_result = curl_easy_perform(curl);
    if (curl_result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    if (curl_result != CURLE_OK) {
        free(response_buffer.data);
        return send_error_json_response(connection, MHD_HTTP_BAD_GATEWAY, "Failed to reach bot service");
    }

    if (!response_buffer.data) {
        return send_error_json_response(connection, MHD_HTTP_BAD_GATEWAY, "Bot service returned empty response");
    }

    result = send_raw_json_response(connection, response_buffer.data, (int)status_code);
    free(response_buffer.data);
    return result;
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

    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *response = MHD_create_response_from_buffer(0, (void *)"", MHD_RESPMEM_PERSISTENT);
        int ret;
        add_cors_headers(response);
        ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }

    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/board") == 0) {
        return forward_request(connection, "GET", "/bot/board", NULL, bot_timeout_seconds);
    }

    if (strcmp(method, "GET") == 0 && strcmp(url, "/api/config") == 0) {
        return forward_request(connection, "GET", "/bot/config", NULL, bot_timeout_seconds);
    }

    if (*con_cls == NULL) {
        ctx = calloc(1, sizeof(struct connection_context));
        if (!ctx) {
            return send_error_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
        }
        *con_cls = ctx;
        return MHD_YES;
    }

    ctx = (struct connection_context *)*con_cls;

    if (strcmp(method, "POST") == 0) {
        if (*upload_data_size > 0) {
            size_t needed = ctx->body_size + *upload_data_size + 1;
            if (needed > MAX_BODY_SIZE) {
                return send_error_json_response(connection, MHD_HTTP_CONTENT_TOO_LARGE, "Request body too large");
            }

            if (ctx->body_capacity < needed) {
                size_t new_capacity = needed * 2;
                char *new_body = realloc(ctx->body, new_capacity);
                if (!new_body) {
                    return send_error_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
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
                return send_error_json_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, "Out of memory");
            }
            ctx->body[0] = '\0';
        } else {
            ctx->body[ctx->body_size] = '\0';
        }

        if (strcmp(url, "/api/move") == 0) {
            return forward_request(connection, "POST", "/bot/move", ctx->body, bot_timeout_seconds);
        }
        if (strcmp(url, "/api/reset") == 0) {
            return forward_request(connection, "POST", "/bot/reset", ctx->body, bot_timeout_seconds);
        }
        if (strcmp(url, "/api/tests") == 0) {
            return forward_request(connection, "POST", "/bot/tests", ctx->body, bot_timeout_seconds);
        }
        if (strcmp(url, "/api/pgn-tags") == 0) {
            return forward_request(connection, "POST", "/bot/pgn-tags", ctx->body, bot_timeout_seconds);
        }

        return send_error_json_response(connection, MHD_HTTP_NOT_FOUND, "Not found");
    }

    return send_error_json_response(connection, MHD_HTTP_BAD_REQUEST, "Method not allowed");
}

int main(void) {
    struct MHD_Daemon *daemon;
    const char *env_bot_url = getenv("BOT_URL");
    const char *env_bot_timeout = getenv("BOT_TIMEOUT_SECONDS");

    if (env_bot_url && env_bot_url[0] != '\0') {
        bot_base_url = env_bot_url;
    }
    bot_timeout_seconds = parse_positive_long_or_default(env_bot_timeout, DEFAULT_BOT_TIMEOUT_SECONDS);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return 1;
    }

    daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        API_PORT,
        NULL,
        NULL,
        &request_handler,
        NULL,
        MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
        MHD_OPTION_END
    );

    if (daemon == NULL) {
        curl_global_cleanup();
        fprintf(stderr, "Failed to start API daemon on port %d\n", API_PORT);
        return 1;
    }

    printf("Chess API proxy running on http://0.0.0.0:%d\n", API_PORT);
    printf("Proxying bot requests to: %s\n", bot_base_url);
    printf("Bot request timeout: %lds\n", bot_timeout_seconds);
    printf("Press Ctrl+C to stop.\n");
    fflush(stdout);

    while (1) {
        sleep(60);
    }

    MHD_stop_daemon(daemon);
    curl_global_cleanup();
    return 0;
}
