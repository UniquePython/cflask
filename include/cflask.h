#ifndef CFLASK_H_
#define CFLASK_H_

#include <inttypes.h>
#include <stddef.h>

typedef struct
{
    const char *method;
    const char *path;
    const char *query;
    const char *version;
} Request;

void register_route(const char *method, const char *path, int (*handler)(Request *, int));
char *get_query_param(const char *query, const char *key);
void send_response(int client_fd, int status, const char *status_text, const char *body);
void run_server(uint16_t port, int backlog);

#define HANDLER(name) int name##_handler(Request *req, int client_fd)
#define RESPOND(code, msg, body)                   \
    do                                             \
    {                                              \
        send_response(client_fd, code, msg, body); \
        return code;                               \
    } while (0)

#define GET_QUERY_PARAM(var, key) char *var = get_query_param(req->query, key)

#define OK(body) RESPOND(200, "OK", body)
#define NOT_FOUND(body) RESPOND(404, "Not Found", body)
#define BAD_REQUEST(body) RESPOND(400, "Bad Request", body)

#define _REGISTER_METHOD(method, route) register_route(#method, "/" #route, route##_handler);

#define REGISTER_GET(route) _REGISTER_METHOD(GET, route);
#define REGISTER_POST(route) _REGISTER_METHOD(POST, route);
#define REGISTER_PUT(route) _REGISTER_METHOD(PUT, route);
#define REGISTER_PATCH(route) _REGISTER_METHOD(PATCH, route);
#define REGISTER_DELETE(route) _REGISTER_METHOD(DELETE, route);

#endif