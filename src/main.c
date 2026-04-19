#include "cflask.h"

HANDLER(hello)
{
    OK("hello");
}

HANDLER(about)
{
    OK("about");
}

int main(void)
{
    REGISTER_GET(hello);
    REGISTER_GET(about);

    run_server(8080, 5);

    return 0;
}