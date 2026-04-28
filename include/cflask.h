#ifndef CFLASK_H_
#define CFLASK_H_

#include <inttypes.h>
#include <stddef.h>

#include "json_builder.h"

typedef struct
{
    const char *method;
    const char *path;
    const char *query;
    const char *version;
} Request;

void register_route(const char *method, const char *path, int (*handler)(Request *, int));
char *get_query_param(const char *query, const char *key);
void send_response(int client_fd, int status, const char *status_text, const char *content_type, const char *body);
void run_server(uint16_t port, int backlog);

#define HANDLER(name) int name##_handler(Request *req, int client_fd)
#define RESPOND(code, msg, content_type, body)                   \
    do                                                           \
    {                                                            \
        send_response(client_fd, code, msg, content_type, body); \
        return code;                                             \
    } while (0)

#define GET_QUERY_PARAM(var, key) char *var = get_query_param(req->query, key)

#define CONTENT_TYPE_PLAINTEXT "text/plain"
#define CONTENT_TYPE_JSON "application/json"

#define OK_PLAINTEXT(body) RESPOND(200, "OK", CONTENT_TYPE_PLAINTEXT, body)
#define NOT_FOUND_PLAINTEXT(body) RESPOND(404, "Not Found", CONTENT_TYPE_PLAINTEXT, body)
#define BAD_REQUEST_PLAINTEXT(body) RESPOND(400, "Bad Request", CONTENT_TYPE_PLAINTEXT, body)

#define OK_JSON(body) RESPOND(200, "OK", CONTENT_TYPE_JSON, body)
#define NOT_FOUND_JSON(body) RESPOND(404, "Not Found", CONTENT_TYPE_JSON, body)
#define BAD_REQUEST_JSON(body) RESPOND(400, "Bad Request", CONTENT_TYPE_JSON, body)

#define RESPOND_JSON_BUILD(code, msg, j)                               \
    do                                                                 \
    {                                                                  \
        char *_json = json_build(j);                                   \
        json_free(j);                                                  \
        send_response(client_fd, code, msg, CONTENT_TYPE_JSON, _json); \
        free(_json);                                                   \
        return code;                                                   \
    } while (0)

#define OK_JSON_BUILD(j) RESPOND_JSON_BUILD(200, "OK", j)
#define NOT_FOUND_JSON_BUILD(j) RESPOND_JSON_BUILD(404, "Not Found", j)
#define BAD_REQUEST_JSON_BUILD(j) RESPOND_JSON_BUILD(400, "Bad Request", j)

#define _REGISTER_METHOD(method, route) register_route(#method, "/" #route, route##_handler);

#define REGISTER_GET(route) _REGISTER_METHOD(GET, route);
#define REGISTER_POST(route) _REGISTER_METHOD(POST, route);
#define REGISTER_PUT(route) _REGISTER_METHOD(PUT, route);
#define REGISTER_PATCH(route) _REGISTER_METHOD(PATCH, route);
#define REGISTER_DELETE(route) _REGISTER_METHOD(DELETE, route);

#endif