#include "cflask.h"

HANDLER(greet)
{
    GET_QUERY_PARAM(name, "name");
    if (!name)
        BAD_REQUEST("missing 'name' parameter");

    int status = 200;
    send_response(client_fd, 200, "OK", name);
    free(name);
    return status;
}

int main(void)
{
    REGISTER_GET(greet);

    run_server(8080, 5);

    return 0;
}