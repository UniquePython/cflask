#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

#define PORT 8080
#define BACKLOG 5

#define STATUS_200_OK 200, "OK"

typedef struct
{
    char method[16];
    char path[256];
    void (*handler)(int);
} Route;

Route routes[10];
int route_count = 0;

void register_route(const char *method, const char *path, void (*handler)(int))
{
    strcpy(routes[route_count].method, method);
    strcpy(routes[route_count].path, path);
    routes[route_count].handler = handler;
    route_count++;
}

void send_response(int client_fd, int status, const char *status_text, const char *body)
{
    char response[1024];
    size_t body_len = strlen(body);

    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             status, status_text, body_len, body);

    write(client_fd, response, strlen(response));
}

void dispatch(const char *method, const char *path, int client_fd)
{
    for (int i = 0; i < route_count; i++)
    {
        if (strcmp(routes[i].method, method) == 0 &&
            strcmp(routes[i].path, path) == 0)
        {
            routes[i].handler(client_fd);
            return;
        }
    }

    send_response(client_fd, 404, "Not Found", "404 Not Found");
}

void hello_handler(int client_fd)
{
    send_response(client_fd, STATUS_200_OK, "Hello!");
}

void about_handler(int client_fd)
{
    send_response(client_fd, STATUS_200_OK, "About page");
}

int main()
{
    register_route("GET", "/hello", hello_handler);
    register_route("GET", "/about", about_handler);

    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        return 1;
    }

    printf("Listening on port %d...\n", PORT);

    while (1)
    {
        int client_fd;
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0)
        {
            close(client_fd);
            continue;
        }

        char method[16], path[256];
        sscanf(buffer, "%s %s", method, path);

        dispatch(method, path, client_fd);

        close(client_fd);
    }

    close(server_fd);

    return 0;
}