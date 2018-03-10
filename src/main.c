#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "http.h"

int main(void)
{
    struct http_request request = {
        .host = "www.example.com",
        .path = "/",
    };

    if(http_get_request(&request) > 0) {
        int c;
        while((c = http_getc(&request)) > 0) {
            putchar(c);
        }
    }

    http_close(&request);
}
