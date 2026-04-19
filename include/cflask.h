#ifndef CFLASK_H_
#define CFLASK_H_

void register_route(const char *method, const char *path, void (*handler)(int));
void send_response(int client_fd, int status, const char *status_text, const char *body);
void run_server(int port, int backlog);

#define HANDLER(name) void name##_handler(int client_fd)

#define _REGISTER_METHOD(method, route) register_route(#method, "/" #route, route##_handler);

#define REGISTER_GET(route) _REGISTER_METHOD(GET, route);
#define REGISTER_POST(route) _REGISTER_METHOD(POST, route);
#define REGISTER_PUT(route) _REGISTER_METHOD(PUT, route);
#define REGISTER_PATCH(route) _REGISTER_METHOD(PATCH, route);
#define REGISTER_DELETE(route) _REGISTER_METHOD(DELETE, route);

#endif