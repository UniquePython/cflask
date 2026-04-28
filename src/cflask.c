#define _POSIX_C_SOURCE 200809L

#include "cflask.h"

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    keep_running = 0;
}

typedef struct
{
    const char *method;
    const char *path;
    int (*handler)(Request *, int);
} Route;

Route *routes = NULL;
size_t route_count = 0;
size_t route_capacity = 0;

void register_route(const char *method, const char *path, int (*handler)(Request *, int))
{
    if (route_count == route_capacity)
    {
        size_t new_capacity = route_capacity == 0 ? 4 : route_capacity * 2;
        Route *new_routes = realloc(routes, new_capacity * sizeof(Route));

        if (!new_routes)
        {
            perror("realloc");
            exit(1);
        }

        routes = new_routes;
        route_capacity = new_capacity;
    }

    routes[route_count].method = method;
    routes[route_count].path = path;
    routes[route_count].handler = handler;

    route_count++;
}

static ssize_t write_all(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    const char *p = buf;

    while (total < len)
    {
        ssize_t n = write(fd, p + total, len - total);

        if (n < 0)
        {
            if (errno == EINTR)
                continue; // interrupted -> retry
            return -1;    // real error
        }

        if (n == 0)
            return -1; // shouldn't happen for write, but treat as failure

        total += (size_t)n;
    }

    return (ssize_t)total;
}

static char *url_decode(const char *src)
{
    if (!src)
        return NULL;

    size_t len = strlen(src);
    char *decoded = malloc(len + 1); // decoded is always <= src length
    if (!decoded)
        return NULL;

    size_t i = 0, j = 0;

    while (i < len)
    {
        if (src[i] == '%' && i + 2 < len)
        {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};
            char *end;
            long val = strtol(hex, &end, 16);

            if (end == hex + 2) // both digits were valid hex
            {
                decoded[j++] = (char)val;
                i += 3;
            }
            else // not valid hex — pass through literally
                decoded[j++] = src[i++];
        }
        else if (src[i] == '+')
        {
            decoded[j++] = ' ';
            i++;
        }
        else
            decoded[j++] = src[i++];
    }

    decoded[j] = '\0';
    return decoded;
}

char *get_query_param(const char *query, const char *key)
{
    if (!query || !key)
        return NULL;

    char *q = strdup(query); // a modifiable copy
    if (!q)
        return NULL;

    char *pair = strtok(q, "&");
    while (pair)
    {
        char *eq = strchr(pair, '=');
        if (eq)
        {
            *eq = '\0';
            const char *k = pair;
            const char *v = eq + 1;

            if (strcmp(k, key) == 0)
            {
                char *result = url_decode(v);
                free(q);
                return result;
            }
        }

        pair = strtok(NULL, "&");
    }

    free(q);
    return NULL;
}

void send_response(int client_fd, int status, const char *status_text, const char *content_type, const char *body)
{
    char header[512];
    size_t body_len = strlen(body);

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              status, status_text, content_type, body_len);

    // snprintf error
    if (header_len < 0)
    {
        perror("snprintf");
        return;
    }

    // truncated header (very unlikely)
    if ((size_t)header_len >= sizeof(header))
    {
        fprintf(stderr, "Header too large\n");
        return;
    }

    if (write_all(client_fd, header, (size_t)header_len) < 0)
    {
        perror("write header");
        return;
    }

    if (write_all(client_fd, body, body_len) < 0)
    {
        perror("write body");
        return;
    }
}

static void dispatch(Request *req, int client_fd, struct sockaddr_in *client_addr)
{
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &client_addr->sin_addr, ip, sizeof(ip)))
    {
        strcpy(ip, "unknown");
    }

    char timebuf[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%d/%b/%Y %H:%M:%S", tm_info);

    for (size_t i = 0; i < route_count; i++)
    {
        if (strcmp(routes[i].method, req->method) == 0 && strcmp(routes[i].path, req->path) == 0)
        {
            int status = routes[i].handler(req, client_fd);

            printf("%s - - [%s] \"%s %s%s%s %s\" %d\n",
                   ip, timebuf, req->method, req->path,
                   req->query ? "?" : "",
                   req->query ? req->query : "",
                   req->version, status);
            fflush(stdout);

            return;
        }
    }

    send_response(client_fd, 404, "Not Found", CONTENT_TYPE_PLAINTEXT, "404 Not Found");

    printf("%s - - [%s] \"%s %s %s\" %d\n", ip, timebuf, req->method, req->path, req->version, 404);
    fflush(stdout);
}

#define INITIAL_BUF_SIZE 1024
#define MAX_REQUEST_SIZE 65536 // safety cap (64KB)

static ssize_t read_request(int fd, char **out_buf)
{
    size_t capacity = INITIAL_BUF_SIZE;
    size_t total = 0;

    char *buffer = malloc(capacity);
    if (!buffer)
        return -1;

    // read until we have the full headers
    while (1)
    {
        if (total == capacity - 1)
        {
            if (capacity >= MAX_REQUEST_SIZE)
            {
                free(buffer);
                return -1;
            }
            capacity *= 2;
            char *tmp = realloc(buffer, capacity);
            if (!tmp)
            {
                free(buffer);
                return -1;
            }
            buffer = tmp;
        }

        ssize_t n = read(fd, buffer + total, capacity - 1 - total);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            free(buffer);
            return -1;
        }
        if (n == 0)
            break;

        total += (size_t)n;
        buffer[total] = '\0';

        if (strstr(buffer, "\r\n\r\n"))
            break;
    }

    // if there's a body, read it too
    char *hdr_end = strstr(buffer, "\r\n\r\n");
    if (hdr_end)
    {
        char *cl_header = strstr(buffer, "Content-Length: ");
        if (cl_header && cl_header < hdr_end)
        {
            long content_length = strtol(cl_header + 16, NULL, 10);

            if (content_length > 0)
            {
                size_t body_offset = (size_t)(hdr_end + 4 - buffer);
                size_t body_already = total - body_offset;
                size_t body_remaining = (size_t)content_length - body_already;
                size_t total_needed = total + body_remaining;

                if (total_needed + 1 > MAX_REQUEST_SIZE)
                {
                    free(buffer);
                    return -1;
                }

                if (total_needed + 1 > capacity)
                {
                    char *tmp = realloc(buffer, total_needed + 1);
                    if (!tmp)
                    {
                        free(buffer);
                        return -1;
                    }
                    buffer = tmp;
                    capacity = total_needed + 1;
                }

                while (body_already < (size_t)content_length)
                {
                    ssize_t n = read(fd, buffer + total, (size_t)content_length - body_already);
                    if (n < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        free(buffer);
                        return -1;
                    }
                    if (n == 0)
                        break;
                    total += (size_t)n;
                    body_already += (size_t)n;
                }

                buffer[total] = '\0';
            }
        }
    }

    *out_buf = buffer;
    return (ssize_t)total;
}

void run_server(uint16_t port, int backlog)
{
    signal(SIGINT, handle_signal);  // Ctrl+C
    signal(SIGTERM, handle_signal); // kill
    signal(SIGPIPE, SIG_IGN);       // Ignore SIGPIPE so server doesn’t crash if client disconnects mid-write

    int server_fd;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return;
    }

    if (listen(server_fd, backlog) < 0)
    {
        perror("listen");
        close(server_fd);
        return;
    }

    printf(" * Running CFlask server (https://github.com/UniquePython/cflask)\n");
    printf(" * Running on http://0.0.0.0:%u (all interfaces)\n", (unsigned)port);
    printf(" * Running on http://127.0.0.1:%d\n", port);
    printf("Press CTRL+C to quit\n");
    fflush(stdout);

    while (keep_running)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                if (!keep_running)
                    break;
                continue;
            }
            perror("accept");
            continue;
        }

        char *buffer = NULL;
        ssize_t bytes_read = read_request(client_fd, &buffer);
        if (bytes_read <= 0)
        {
            close(client_fd);
            free(buffer);
            continue;
        }

        Request req = {0};

        // --- body parsing (must happen before strtok) ---
        char *hdr_end = strstr(buffer, "\r\n\r\n");
        const char *body_ptr = NULL;
        size_t body_len = 0;

        if (hdr_end)
        {
            char *cl_header = strstr(buffer, "Content-Length: ");
            if (cl_header && cl_header < hdr_end)
            {
                long content_length = strtol(cl_header + 16, NULL, 10);
                if (content_length > 0)
                {
                    body_ptr = hdr_end + 4;
                    body_len = (size_t)content_length;
                }
            }
        }
        // -------------------------------------------------

        req.method = strtok(buffer, " ");
        req.path = strtok(NULL, " ");
        req.version = strtok(NULL, "\r\n");

        if (!req.method || !req.path || !req.version)
        {
            send_response(client_fd, 400, "Bad Request", CONTENT_TYPE_PLAINTEXT, "400 Bad Request");
            close(client_fd);
            free(buffer);
            continue;
        }

        if (strcmp(req.method, "GET") != 0 &&
            strcmp(req.method, "POST") != 0 &&
            strcmp(req.method, "PUT") != 0 &&
            strcmp(req.method, "PATCH") != 0 &&
            strcmp(req.method, "DELETE") != 0)
        {
            send_response(client_fd, 405, "Method Not Allowed", CONTENT_TYPE_PLAINTEXT, "Method Not Allowed");
            close(client_fd);
            free(buffer);
            continue;
        }

        if (strncmp(req.version, "HTTP/", 5) != 0)
        {
            send_response(client_fd, 400, "Bad Request", CONTENT_TYPE_PLAINTEXT, "Invalid HTTP version");
            close(client_fd);
            free(buffer);
            continue;
        }

        char *qmark = strchr(req.path, '?');
        if (qmark)
        {
            *qmark = '\0';
            req.query = qmark + 1;
        }
        else
            req.query = NULL;

        // assign body after all strtok/parsing is done
        req.body = body_ptr;
        req.body_len = body_len;

        dispatch(&req, client_fd, &client_addr);

        free(buffer);
        close(client_fd);
    }

    printf("\nShutting down server...\n");
    fflush(stdout);

    free(routes);
    close(server_fd);
}