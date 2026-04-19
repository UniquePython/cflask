#include "cflask.h"

HANDLER(hello)
{
    send_response(client_fd, 200, "OK", "hello");
}

HANDLER(about)
{
    send_response(client_fd, 200, "OK", "about");
}

int main(void)
{
    REGISTER_GET(hello);
    REGISTER_GET(about);

    run_server(8080, 5);

    return 0;
}