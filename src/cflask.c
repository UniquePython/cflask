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

typedef struct
{
    char *method;
    char *path;
    int (*handler)(int);
} Route;

Route *routes = NULL;
int route_count = 0;
int route_capacity = 0;

void register_route(const char *method, const char *path, int (*handler)(int))
{
    if (route_count == route_capacity)
    {
        int new_capacity = route_capacity == 0 ? 4 : route_capacity * 2;
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

        total += n;
    }

    return total;
}

void send_response(int client_fd, int status, const char *status_text, const char *body)
{
    char header[512];
    size_t body_len = strlen(body);

    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              status, status_text, body_len);

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

    if (write_all(client_fd, header, header_len) < 0)
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

static void dispatch(const char *method, const char *path, int client_fd, struct sockaddr_in *client_addr)
{
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip, sizeof(ip));

    char timebuf[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%d/%b/%Y %H:%M:%S", tm_info);

    for (int i = 0; i < route_count; i++)
    {
        if (strcmp(routes[i].method, method) == 0 && strcmp(routes[i].path, path) == 0)
        {
            int status = routes[i].handler(client_fd);

            printf("%s - - [%s] \"%s %s HTTP/1.1\" %d\n", ip, timebuf, method, path, status);

            return;
        }
    }

    send_response(client_fd, 404, "Not Found", "404 Not Found");

    printf("%s - - [%s] \"%s %s HTTP/1.1\" %d\n", ip, timebuf, method, path, 404);
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

    while (1)
    {
        if (total == capacity - 1)
        {
            if (capacity >= MAX_REQUEST_SIZE)
            {
                free(buffer);
                return -1; // too large
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

        total += n;
        buffer[total] = '\0';

        if (strstr(buffer, "\r\n\r\n"))
            break;
    }

    *out_buf = buffer;
    return total;
}

void run_server(int port, int backlog)
{
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
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    printf(" * Running CFlask server (https://github.com/UniquePython/cflask)");
    printf(" * Running on http://0.0.0.0:%d (all interfaces)\n", port);
    printf(" * Running on http://127.0.0.1:%d\n", port);
    printf("Press CTRL+C to quit\n");

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
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

        char *method = strtok(buffer, " ");
        char *path = strtok(NULL, " ");

        if (!method || !path)
        {
            close(client_fd);
            free(buffer);
            continue;
        }

        dispatch(method, path, client_fd, &client_addr);

        free(buffer);

        close(client_fd);
    }

    free(routes);

    close(server_fd);
}